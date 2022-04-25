const {
    fromZigbeeConverters,
    toZigbeeConverters,
    exposes
} = require('zigbee-herdsman-converters');
const ZCL_DATATYPE_INT16 = 0x29;
const ZCL_DATATYPE_UINT16 = 0x21;
const ZCL_DATATYPE_BOOLEAN = 0x10;
const ZCL_DATATYPE_INT32 = 0x2b;
const bind = async (endpoint, target, clusters) => {
    for (const cluster of clusters) {
        await endpoint.bind(cluster, target);
    }
};

const ACCESS_STATE = 0b001, ACCESS_WRITE = 0b010, ACCESS_READ = 0b100;

const fz_peep = {
    peep_us100: {
        cluster: 'msIlluminanceMeasurement',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            return {
                distance_in_mm: msg.data[0xF001],
            };
        },
    },
};

const device = {
    zigbeeModel: ['US100_Ultrasonic'],
        model: 'US100_Ultrasonic',
        vendor: 'Diddlik',
        description: 'Ultrasonic Sensor',
        fromZigbee: [fz_peep.peep_us100, fromZigbeeConverters.temperature, fromZigbeeConverters.battery],
        toZigbee: [toZigbeeConverters.factory_reset],
        meta: {multiEndpoint: true},
		endpoint: (device) => {
            return {'us100': 1, 'ds': 2};
        },
		configure: async (device, coordinatorEndpoint, logger) => {
            const firstEndpoint = device.getEndpoint(1);
            const secondEndpoint = device.getEndpoint(2);
            await bind(firstEndpoint, coordinatorEndpoint, ['genPowerCfg', 'msTemperatureMeasurement', 'msIlluminanceMeasurement']);
            await bind(secondEndpoint, coordinatorEndpoint, ['msTemperatureMeasurement']);
			const genPowerCfgPayload = [{
                attribute: 'batteryVoltage',
                minimumReportInterval: 0,
                maximumReportInterval: 3600,
                reportableChange: 0,
            },
            {
                attribute: 'batteryPercentageRemaining',
                minimumReportInterval: 0,
                maximumReportInterval: 3600,
                reportableChange: 0,
            }
        ];

        const msBindPayload = [{
            attribute: 'measuredValue',
            minimumReportInterval: 0,
            maximumReportInterval: 3600,
            reportableChange: 0,
        }];
		await firstEndpoint.configureReporting('genPowerCfg', genPowerCfgPayload);
		await firstEndpoint.configureReporting('msTemperatureMeasurement', msBindPayload);
		await secondEndpoint.configureReporting('msTemperatureMeasurement', msBindPayload);
		
            //await reporting.batteryVoltage(firstEndpoint, overides);
            //await reporting.batteryPercentageRemaining(firstEndpoint, overides);
            //await reporting.temperature(firstEndpoint, overides);
            //await reporting.temperature(secondEndpoint, overides);
		const payload = [{
			attribute: {ID: 0xF001, type: 0x21}, 
			minimumReportInterval: 0, 
			maximumReportInterval: 3600, 
			reportableChange: 0,
			}];
        await firstEndpoint.configureReporting('msIlluminanceMeasurement', payload);
        },
		exposes: [exposes.numeric('battery', ACCESS_STATE).withUnit('%'), exposes.numeric('temperature', ACCESS_STATE).withEndpoint('ds').withUnit('C°'), exposes.numeric('temperature', ACCESS_STATE).withEndpoint('us100').withUnit('C°'), 
			exposes.numeric('distance_in_mm', ACCESS_STATE).withUnit('mm').withDescription('Distance in mm'),
		],
};

module.exports = device;
