/**
 * \file
 *
 * \brief Empty user application template
 *
 */

/**
 * \mainpage User Application template doxygen documentation
 *
 * \par Empty user application template
 *
 * Bare minimum empty user application template
 *
 * \par Content
 *
 * -# Include the ASF header files (through asf.h)
 * -# Minimal main function that starts with a call to system_init()
 * -# "Insert application code here" comment
 *
 */

/*
 * Include header files for all drivers that have been imported from
 * Atmel Software Framework (ASF).
 */
/*
 * Support and FAQ: visit <a href="https://www.microchip.com/support/">Microchip Support</a>
 */
#include <asf.h>
#include <s2c_utils.h>

// Function prototypes
uint8_t get_pinstrap_id(void);
inline float uint16ToC(uint16_t data);
void flash_pinstrap_id(uint8_t pin_num);
void flash_low();
void flash_high();
void quick_blink();

void configure_adc(void);
void configure_can(void);
void configure_i2c(void);

void adc_callback(struct adc_module *const module);

void loop_adc(void);
void loop_i2c(void);
void loop_can(void);

// Board management variables
uint8_t board_id = 255;
enum s2c_board_type board_type = S2C_BOARD_OTHER;
struct s2c_board_config board_config;

bool FLAHS_OUT_EXTRA_INFO = true;

// ASF driver instances
struct adc_module adc_instance;
struct can_module can_instance;
struct i2c_master_module i2c_instance;

// ADC variables
uint16_t adc_sample_buffer[ADC_NUM_SAMPLES] = {0}; // stores single channel conversion samples
uint32_t adc_channel[ADC_NUM_CHANNELS] = {AN0, AN1, AN2, AN3}; // stores ADC input pins in the order that they will be read
uint16_t adc_channel_vals[ADC_NUM_CHANNELS] = {0}; // stores the final averaged value of each channel's conversion
uint8_t adc_channel_index = 0; // index of current channel being read
bool adc_section_done = false; // true when all adc cannels have been read

// I2C variables
struct i2c_master_packet wr_packet, rd_packet;
struct i2c_master_module i2c_master_instance;
uint16_t i2c_temperature_vals[I2C_NUM_TEMP_SENSORS] = {0};
static uint8_t registerAddress = 0x07;
static uint8_t buffer[8];
uint16_t ret;
bool i2c_section_done = false;
enum status_code i2c_status_code;

// CAN variables
//TODO
bool can_received = false;



uint8_t get_pinstrap_id(void) {
	static uint8_t id = 255;
	
	if(id == 255) { //analog write 255
		int input = port_group_get_input_level(&PORTA, PINSTRAPS);
		id =	((input & PINSTRAP_0) > 0) | 
				(((input & PINSTRAP_1) > 0) << 1) | // ??? Why 3 different less thans???
				(((input & PINSTRAP_2) > 0) << 2) | 
				(((input & PINSTRAP_3) > 0) << 3);
	}
	return id;
}


inline float uint16ToC(uint16_t data) {
	
	return (float)data * 0.02 - 273.15; //formula for temperature conversion
}




void flash_high(){
	port_pin_set_output_level(LED_USER_PIN, true); // turn on led
	delay_ms(600);
	port_pin_set_output_level(LED_USER_PIN, false);
	delay_ms(400);
} //long pauses


void flash_low(){
	port_pin_set_output_level(LED_USER_PIN, true);
	delay_ms(50);
	port_pin_set_output_level(LED_USER_PIN, false);
	delay_ms(450);
}//shorter pauses



void quick_blink(){
	port_pin_toggle_output_level(LED_USER_PIN);
	delay_ms(30);
	port_pin_toggle_output_level(LED_USER_PIN);
	delay_ms(30);
}

/*
flashes out the binary of the board ID
Warning long delay
*/
void flash_pinstrap_id(uint8_t pin_num){ ///??? this part why 2 for loops???
	int blink_amount = 5;
	for(int i =0; i <blink_amount; i++){
		quick_blink();
	}
	port_pin_set_output_level(LED_USER_PIN, false);
	delay_ms(1000);
	
	uint8_t temp_pin = pin_num;
	for(int i =0; i<4;i++){
		if (temp_pin & 1) {
			flash_high();
			} else {
			flash_low();
		}
		temp_pin = temp_pin >> 1;//shift over
	}
	
}



// Configuration functions

void configure_adc(void) {
	struct adc_config config;
	adc_get_config_defaults(&config);
	
	config.clock_prescaler = ADC_CLOCK_PRESCALER_DIV8;
	config.reference       = ADC_REFERENCE_INTVCC2;
	config.positive_input  = ADC_POSITIVE_INPUT_PIN5;
	config.resolution      = ADC_RESOLUTION_10BIT;
	
	adc_init(&adc_instance, ADC0, &config);
	
	adc_enable(&adc_instance);
	
	adc_register_callback(&adc_instance, adc_callback, ADC_CALLBACK_READ_BUFFER);
	adc_enable_callback(&adc_instance, ADC_CALLBACK_READ_BUFFER);
} //initiliaze clocks I guess??

void configure_i2c(void) {
	struct i2c_master_config config_i2c;
	i2c_master_get_config_defaults(&config_i2c);
	
	config_i2c.pinmux_pad0 = I2C_SDA_PIN;
	config_i2c.pinmux_pad1 = I2C_SCL_PIN;
	config_i2c.buffer_timeout = 200;


	/* Initialize and enable device with config */
	while(i2c_master_init(&i2c_master_instance, SERCOM2, &config_i2c) != STATUS_OK);

	i2c_master_enable(&i2c_master_instance);
	
	wr_packet.data_length = 1;
	wr_packet.data = &registerAddress;
} 

void configure_can(void) {
	/* Set up the CAN TX/RX pins */
	struct system_pinmux_config pin_config;
	system_pinmux_get_config_defaults(&pin_config);
	pin_config.mux_position = CAN_TX_MUX_SETTING;
	system_pinmux_pin_set_config(CAN_TX_PIN, &pin_config);
	pin_config.mux_position = CAN_RX_MUX_SETTING;
	system_pinmux_pin_set_config(CAN_RX_PIN, &pin_config);

	/* Initialize the module. */
	struct can_config config_can;
	can_get_config_defaults(&config_can);
	can_init(&can_instance, CAN_MODULE, &config_can);

	can_start(&can_instance);

	/* Enable interrupts for this CAN module */
	system_interrupt_enable(SYSTEM_INTERRUPT_MODULE_CAN0);
	can_enable_interrupt(&can_instance, CAN_PROTOCOL_ERROR_ARBITRATION
	| CAN_PROTOCOL_ERROR_DATA);
	
	/* Set standby pin LOW on transceiver */
	struct port_config config_port;
	port_get_config_defaults(&config_port);
	config_port.direction = PORT_PIN_DIR_OUTPUT;
	config_port.input_pull = PORT_PIN_PULL_NONE;
	port_pin_set_config(CAN_STBY_PIN, &config_port);
	port_pin_set_output_level(CAN_STBY_PIN, false);
}//CANBUS initiliazations ??



void adc_callback(struct adc_module *const module) {

	adc_channel_vals[adc_channel_index] = 0;
	for(int i = 0; i < ADC_NUM_SAMPLES; i++) {
		adc_channel_vals[adc_channel_index] += adc_sample_buffer[i];	
	}
	adc_channel_vals[adc_channel_index] >>= ADC_SAMPLE_DIV;
	
	
	if(adc_channel_index < board_config.adc_channels - 1) {
		++adc_channel_index;
		adc_set_positive_input(&adc_instance, adc_channel[adc_channel_index]);
		adc_read_buffer_job(&adc_instance, adc_sample_buffer, ADC_NUM_SAMPLES);
		
	} else {
		adc_section_done = true;
		adc_channel_index = 0;
	}
}//read sensors and channels and store information???



void loop_adc(void) {
	
	if(adc_channel_index == 0) {
		adc_set_positive_input(&adc_instance, adc_channel[adc_channel_index]);
		adc_read_buffer_job(&adc_instance, adc_sample_buffer, ADC_NUM_SAMPLES);
	}
}

void loop_i2c(void) { // update statuses??
	
	switch(board_type) {
	case S2C_BOARD_WHEEL:
		wr_packet.address = I2C_MLX_WHEEL_ID;
		//rd_packet.address = SENSOR1_ADDRESS;
		while((i2c_status_code = i2c_master_write_packet_wait_no_stop(&i2c_master_instance, &wr_packet)) == STATUS_BUSY);
		if(i2c_status_code == STATUS_OK) {
			while(i2c_master_read_packet_wait(&i2c_master_instance, &rd_packet) == STATUS_BUSY);
			i2c_temperature_vals[I2C_INNER_TEMP] = uint16ToC(buffer[0] | buffer[1] << 8);
		}
		break;
			
	case S2C_BOARD_TIRE_TEMP:
		// read sensor 1 (0x5A) (INNER)
		wr_packet.address = I2C_MLX_INNER_ID;
		while((i2c_status_code = i2c_master_write_packet_wait_no_stop(&i2c_master_instance, &wr_packet)) == STATUS_BUSY);
		if(i2c_status_code == STATUS_OK) {
			while(i2c_master_read_packet_wait(&i2c_master_instance, &rd_packet) == STATUS_BUSY);
			i2c_temperature_vals[I2C_INNER_TEMP] = buffer[0] | buffer[1] << 8;
			//printf("Inner Band Temperature: %.2f\n", temp);
		}
			
		// read sensor 2 (0x5B) (MIDDLE)
		wr_packet.address = I2C_MLX_MIDDLE_ID;
		while((i2c_status_code = i2c_master_write_packet_wait_no_stop(&i2c_master_instance, &wr_packet)) == STATUS_BUSY);
		if(i2c_status_code == STATUS_OK) {
			while(i2c_master_read_packet_wait(&i2c_master_instance, &rd_packet) == STATUS_BUSY);
			i2c_temperature_vals[I2C_MIDDLE_TEMP] = buffer[0] | buffer[1] << 8;
			//printf("Middle Band Temperature: %.2f\n", temp);
		}
			
		// read sensor 3 (0x5C) (OUTER)
		wr_packet.address = I2C_MLX_OUTER_ID;
		while((i2c_status_code = i2c_master_write_packet_wait_no_stop(&i2c_master_instance, &wr_packet)) == STATUS_BUSY);
		if(i2c_status_code == STATUS_OK) {
			while(i2c_master_read_packet_wait(&i2c_master_instance, &rd_packet) == STATUS_BUSY);
			i2c_temperature_vals[I2C_OUTER_TEMP] = buffer[0] | buffer[1] << 8;
		}
		break;
	case S2C_BOARD_RADIATOR:
	case S2C_BOARD_OTHER:
	default:
		// do nothing
		break;
	}
	i2c_section_done = true;
}

void loop_can(void) {//??? more temperature conversions and potentiometer and wheel sensor
	struct can_tx_element tx_elem;
	can_get_tx_buffer_element_defaults(&tx_elem);
	//tx_elem.T0.bit.XTD = 1;
	
	switch(board_type) {
	case S2C_BOARD_WHEEL:
		tx_elem.T1.bit.DLC = 4;
		tx_elem.T0.reg = CAN_TX_ELEMENT_T0_STANDARD_ID(CAN_MSG_ID(board_id, 0));
		convert_16_bit_to_byte_array(adc_channel_vals[0], tx_elem.data);
		convert_16_bit_to_byte_array(i2c_temperature_vals[I2C_BRAKE_TEMP], tx_elem.data + 2);
		break;
		
	case S2C_BOARD_TIRE_TEMP:
		tx_elem.T1.bit.DLC = 6;
		tx_elem.T0.reg = CAN_TX_ELEMENT_T0_STANDARD_ID(CAN_MSG_ID(board_id, 0));
		convert_16_bit_to_byte_array(i2c_temperature_vals[I2C_OUTER_TEMP], tx_elem.data);
		convert_16_bit_to_byte_array(i2c_temperature_vals[I2C_MIDDLE_TEMP], tx_elem.data + 2);
		convert_16_bit_to_byte_array(i2c_temperature_vals[I2C_INNER_TEMP], tx_elem.data + 4);
		break;
		
	case S2C_BOARD_RADIATOR:
		tx_elem.T1.bit.DLC = 4;
		tx_elem.T0.reg = CAN_TX_ELEMENT_T0_STANDARD_ID(CAN_MSG_ID(board_id, 0));
		convert_16_bit_to_byte_array(adc_channel_vals[0], tx_elem.data);
		convert_16_bit_to_byte_array(adc_channel_vals[1], tx_elem.data + 2);
		
		/*//Dummy values
		tx_elem.data[0] = 0x80 & 0xFF;
		tx_elem.data[1] = 0x30 & 0xFF;
		can_set_tx_buffer_element(&can_instance, &tx_elem, 0);
		can_tx_transfer_request(&can_instance, 1);*/
		break;
		
	case S2C_BOARD_LINEAR_POT:
		tx_elem.T1.bit.DLC = 2;
		tx_elem.T0.reg = CAN_TX_ELEMENT_T0_STANDARD_ID(CAN_MSG_ID(board_id, 0));
		convert_16_bit_to_byte_array(adc_channel_vals[0], tx_elem.data);
		break;
		
	case S2C_BOARD_STEERING_WHEEL:
		tx_elem.T1.bit.DLC = 2;
		tx_elem.T0.reg = CAN_TX_ELEMENT_T0_STANDARD_ID(CAN_MSG_ID(board_id, 0));
		convert_16_bit_to_byte_array(adc_channel_vals[0], tx_elem.data);
		break;
	}
	
	// only send if tx_element has been configured i.e. if ID has been set
	if(tx_elem.T0.bit.ID > 0) {
		can_set_tx_buffer_element(&can_instance, &tx_elem, 0);
		can_tx_transfer_request(&can_instance, 1);
	}
}


int main (void)
{
	system_init();

	// If code is configured to use pinstraps, do so. If not, leave at default
	board_id = get_pinstrap_id();

	board_type = get_board_type_from_id(board_id);
	
	switch(board_type) {
	case S2C_BOARD_WHEEL:
		S2C_BOARD_WHEEL_CONFIG(board_config);
		break;
	
	case S2C_BOARD_TIRE_TEMP:
		S2C_BOARD_TIRE_TEMP_CONFIG(board_config);
		break;
		
	case S2C_BOARD_RADIATOR:
		S2C_BOARD_RADIATOR_CONFIG(board_config);
		break;
		
	case S2C_BOARD_LINEAR_POT:
		S2C_BOARD_LINEAR_POT_CONFIG(board_config);
		break;
	
	case S2C_BOARD_STEERING_WHEEL:
		S2C_BOARD_STEERING_WHEEL_CONFIG(board_config);
		break;
	
	case S2C_BOARD_OTHER://Do a slow toggle of the light to let people know that it is registered as an other board (currently other board should do nothing).
		while(1){
			port_pin_toggle_output_level(LED_USER_PIN);
			delay_ms(1000);
		}
		break;
	
	}
	//?? This part ???
	Assert(board_config.adc_channels <= ADC_NUM_CHANNELS);
	
	// Configure ADC and I2C depending on board configuration
	if(board_config.use_adc) {
		configure_adc();
	}
	if(board_config.use_i2c) {
		configure_i2c();
	}
	configure_can(); 
	
	system_interrupt_enable_global();
	
	if(FLAHS_OUT_EXTRA_INFO){
		flash_pinstrap_id(board_id);
	}
	
	
	port_pin_set_output_level(LED_USER_PIN, true);
	
	while (1) {
		// check what is being used then loop it and send data in the last if statement
		
		if(board_config.use_adc) loop_adc();
		if(board_config.use_i2c) loop_i2c();
		
		
		if((!board_config.use_adc || adc_section_done) && 
			(!board_config.use_i2c || i2c_section_done)) {
			loop_can();
			delay_ms(20);
			adc_section_done = i2c_section_done = false;
		}
	}
}

void CAN0_Handler(void) //??? This part ???
{
	volatile uint32_t status;
	status = can_read_interrupt_status(&can_instance);
	
	if ((status & CAN_PROTOCOL_ERROR_ARBITRATION) || (status & CAN_PROTOCOL_ERROR_DATA)) {
		can_clear_interrupt_status(&can_instance, CAN_PROTOCOL_ERROR_ARBITRATION | CAN_PROTOCOL_ERROR_DATA);
	
		if(FLAHS_OUT_EXTRA_INFO){
			quick_blink();
		}
	}
}
