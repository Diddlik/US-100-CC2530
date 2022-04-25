
#include "AF.h"
#include "OSAL.h"
#include "OSAL_Clock.h"
#include "OSAL_PwrMgr.h"
#include "ZComDef.h"
#include "ZDApp.h"
#include "ZDNwkMgr.h"
#include "ZDObject.h"
#include "math.h"

#include "nwk_util.h"
#include "zcl.h"
#include "zcl_app.h"
#include "zcl_diagnostic.h"
#include "zcl_general.h"
#include "zcl_lighting.h"
#include "zcl_ms.h"

#include "bdb.h"
#include "bdb_interface.h"
#include "gp_interface.h"

#include "Debug.h"

#include "OnBoard.h"

/* HAL */
#include "ds18b20.h"
#include "hal_adc.h"
#include "hal_drivers.h"
#include "hal_i2c.h"
#include "hal_key.h"
#include "hal_led.h"

#include "battery.h"
#include "commissioning.h"
#include "factory_reset.h"
#include "us100.h"
#include "utils.h"
#include "version.h"

/*********************************************************************
 * MACROS
 */
#define HAL_KEY_CODE_RELEASE_KEY HAL_KEY_CODE_NOKEY

// use led4 as output pin, osal will shitch it low when go to PM
#define POWER_ON_SENSORS()                                                                                                                 \
    do {                                                                                                                                   \
        HAL_TURN_ON_LED4();                                                                                                                \
        IO_PUD_PORT(DS18B20_PORT, IO_PUP);                                                                                                 \
    } while (0)
#define POWER_OFF_SENSORS()                                                                                                                \
    do {                                                                                                                                   \
        HAL_TURN_OFF_LED4();                                                                                                               \
        IO_PUD_PORT(DS18B20_PORT, IO_PDN);                                                                                                 \
    } while (0)

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */

extern bool requestNewTrustCenterLinkKey;
byte zclApp_TaskID;

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static uint8 us100_counter = 0;
static bool us100_state = false;
static bool ds10b20_state = false;
uint16 current_distance = 0;
uint8 current_phase = NOTHING;


afAddrType_t inderect_DstAddr = {.addrMode = (afAddrMode_t)AddrNotPresent, .endPoint = 0, .addr.shortAddr = 0};

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void zclApp_HandleKeys(byte shift, byte keys);
static void zclApp_Report(void);

static void zclApp_ReadSensors(void);
static void zclApp_ReadDS18B20(void);
static void zclApp_InitUS100Uart(void);
static void UartProcessData (uint8 port, uint8 event);
static void zclApp_PowerOffSensors(void);
static void zclApp_Sleep(void);

/*********************************************************************
 * ZCL General Profile Callback table
 */
static zclGeneral_AppCallbacks_t zclApp_CmdCallbacks = {
    NULL, // Basic Cluster Reset command
    NULL, // Identify Trigger Effect command
    NULL, // On/Off cluster commands
    NULL, // On/Off cluster enhanced command Off with Effect
    NULL, // On/Off cluster enhanced command On with Recall Global Scene
    NULL, // On/Off cluster enhanced command On with Timed Off
    NULL, // RSSI Location command
    NULL  // RSSI Location Response command
};

void zclApp_Init(byte task_id) {
  
    IO_PUD_PORT(DS18B20_PORT, IO_PUP);
    POWER_ON_SENSORS();

    HalI2CInit();
    // this is important to allow connects throught routers
    // to make this work, coordinator should be compiled with this flag #define TP2_LEGACY_ZC
    requestNewTrustCenterLinkKey = FALSE;

    zclApp_TaskID = task_id;

    zclGeneral_RegisterCmdCallbacks(1, &zclApp_CmdCallbacks);
    zcl_registerAttrList(zclApp_FirstEP.EndPoint, zclApp_AttrsFirstEPCount, zclApp_AttrsFirstEP);
    bdb_RegisterSimpleDescriptor(&zclApp_FirstEP);

    zcl_registerAttrList(zclApp_SecondEP.EndPoint, zclApp_AttrsSecondEPCount, zclApp_AttrsSecondEP);
    bdb_RegisterSimpleDescriptor(&zclApp_SecondEP);

    zcl_registerForMsg(zclApp_TaskID);

    // Register for all key events - This app will handle all key events
    RegisterForKeys(zclApp_TaskID);
    LREP("Started build %s \r\n", zclApp_DateCodeNT);

    zclApp_InitUS100Uart();
    
    osal_start_reload_timer(zclApp_TaskID, APP_REPORT_EVT, APP_REPORT_DELAY);
}


static void zclApp_InitUS100Uart(void) {
    halUARTCfg_t halUARTConfig;
    halUARTConfig.configured = TRUE;
    halUARTConfig.baudRate = HAL_UART_BR_9600; //9600
    halUARTConfig.flowControl = FALSE;
    halUARTConfig.flowControlThreshold = 48; // this parameter indicates number of bytes left before Rx Buffer
                                             // reaches maxRxBufSize
    halUARTConfig.idleTimeout = 20;          // this parameter indicates rx timeout period in millisecond
    halUARTConfig.rx.maxBufSize = 15;
    halUARTConfig.tx.maxBufSize = 15;
    halUARTConfig.intEnable = TRUE;
    halUARTConfig.callBackFunc = UartProcessData;
    HalUARTInit();
    if (HalUARTOpen(US100_UART_PORT, &halUARTConfig) == HAL_UART_SUCCESS) {
        LREPMaster("Initialized US100 UART \r\n");
    }
    else{
      LREPMaster("Initialization failed US100 UART \r\n");
    }
    
}

static void zclApp_PowerOffSensors(void)
{
  if(ds10b20_state && us100_state)
  {
    LREPMaster("Sensor Power OFF \r\n");
    current_phase = NOTHING;
    ds10b20_state = false;
    us100_state = false;
    osal_start_timerEx(zclApp_TaskID, APP_SLEEP_EVT, 500);
  }
  
}


uint16 zclApp_event_loop(uint8 task_id, uint16 events) {
    afIncomingMSGPacket_t *MSGpkt;

    (void)task_id; // Intentionally unreferenced parameter
    if (events & SYS_EVENT_MSG) {
        while ((MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive(zclApp_TaskID))) {
            switch (MSGpkt->hdr.event) {
            case KEY_CHANGE:
                zclApp_HandleKeys(((keyChange_t *)MSGpkt)->state, ((keyChange_t *)MSGpkt)->keys);
                break;
            case ZCL_INCOMING_MSG:
                if (((zclIncomingMsg_t *)MSGpkt)->attrCmd) {
                    osal_mem_free(((zclIncomingMsg_t *)MSGpkt)->attrCmd);
                }
                break;

            default:
                break;
            }
            // Release the memory
            osal_msg_deallocate((uint8 *)MSGpkt);
        }
        // return unprocessed events
        return (events ^ SYS_EVENT_MSG);
    }

    if (events & APP_REPORT_EVT) {
        osal_pwrmgr_task_state(zclApp_TaskID, PWRMGR_HOLD);  
        POWER_ON_SENSORS();
        LREPMaster("APP_REPORT_EVT\r\n");
        zclApp_Report();
        return (events ^ APP_REPORT_EVT);
    }

    if (events & APP_READ_SENSORS_EVT) {
        LREPMaster("APP_READ_SENSORS_EVT\r\n");
        zclApp_ReadSensors();
        return (events ^ APP_READ_SENSORS_EVT);
    }
    if (events & APP_SLEEP_EVT) {
        LREPMaster("APP_SLEEP_EVT\r\n");
        zclApp_Sleep();
        return (events ^ APP_SLEEP_EVT);
    }
    // Discard unknown events
    return 0;
}

static void zclApp_HandleKeys(byte portAndAction, byte keyCode) {
    LREP("zclApp_HandleKeys portAndAction=0x%X keyCode=0x%X\r\n", portAndAction, keyCode);
    zclFactoryResetter_HandleKeys(portAndAction, keyCode);
    zclCommissioning_HandleKeys(portAndAction, keyCode);
    if (portAndAction & HAL_KEY_PRESS) {
        LREPMaster("Key press\r\n");
        osal_start_timerEx(zclApp_TaskID, APP_REPORT_EVT, 200);
    }
}

static void zclApp_Sleep(void){
  
    POWER_OFF_SENSORS();
    osal_pwrmgr_task_state(zclApp_TaskID, PWRMGR_CONSERVE);
    
}

void UartProcessData (uint8 port, uint8 event)
{
  if (event &(HAL_UART_RX_FULL|HAL_UART_RX_ABOUT_FULL|HAL_UART_RX_TIMEOUT))
    {
      LREPMaster("UART Recieved \r\n");
      
      uint8 ch,count=0;
      uint8 buf[2];
      uint16 value=0;
      
      while (Hal_UART_RxBufLen(port))
        {
          HalUARTRead (port, &ch, 1);
          buf[count++]=ch; //copy the received byte
        }
      
      for(uint8 i=0;i<count;i++)
          printf("%x\r\n", buf[i]);

      if(count == 2)
      {
          value = buf[0] * 256 + buf[1];
      }
      else if(count == 1)
      {
          value = buf[0];
      }
      
      printf("value=%d\r\n", value);
      
      switch(current_phase) {
        case DISTANCE_MEASURMENT:
          if(us100_counter++ < 3)
            {
              if((value > 1) && (value < 10000)){
                current_distance = (value > current_distance ) ? value : current_distance;
                printf("current_distance=%d\r\n", current_distance);
              }
              US100_RequestMeasureDistance();
            }
          else
          {
            printf("Distance=%d\r\n", current_distance);
            LREP("Distance=%d \r\n", current_distance);
            zclApp_DistanceSensor_MeasuredValue = current_distance;
            bdb_RepChangedAttrValue(zclApp_FirstEP.EndPoint, ILLUMINANCE, ATTRID_DISTANCE_MEASURED_VALUE);
            current_distance = 0;
            printf("Request Temperature\r\n");
            us100_counter = 0;
            US100_RequestMeasureTemp();   
          }
            break;       
        case TEMPERATURE_MEASURMENT: 
          if((value > 1) && (value < 130)) // temprature is in range
            {
              value -= 45; // correct 45º offset
              printf("Temperature=%d\r\n", value);
              zclApp_Temperature_Sensor_MeasuredValue = value * 100;
              bdb_RepChangedAttrValue(zclApp_FirstEP.EndPoint, TEMP, ATTRID_MS_TEMPERATURE_MEASURED_VALUE);
              us100_state = true;
            }
             break;
        }
     }

  zclApp_PowerOffSensors();
}

static void zclApp_ReadSensors(void) {

  HalLedSet(HAL_LED_1, HAL_LED_MODE_BLINK);
  printf("US100_RequestMeasureDistance\r\n");
  US100_RequestMeasureDistance();
  zclApp_ReadDS18B20();
  zclBattery_Report();
}

   

static void zclApp_ReadDS18B20(void) {
    int16 temp = readTemperature();
    if (temp != 1) {
        zclApp_DS18B20_MeasuredValue = temp;
        LREP("ReadDS18B20 t=%d\r\n", zclApp_DS18B20_MeasuredValue);
        printf("ReadDS18B20 t=%d\r\n", zclApp_DS18B20_MeasuredValue);
        bdb_RepChangedAttrValue(zclApp_SecondEP.EndPoint, TEMP, ATTRID_MS_TEMPERATURE_MEASURED_VALUE);
    } else {
        LREPMaster("ReadDS18B20 error\r\n");
    }
    ds10b20_state = true;
    zclApp_PowerOffSensors();
}

static void zclApp_Report(void) { osal_start_timerEx(zclApp_TaskID, APP_READ_SENSORS_EVT, 500); }

/****************************************************************************
****************************************************************************/
