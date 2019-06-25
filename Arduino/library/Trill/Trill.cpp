/*****************************
 * Trill library for Arduino
 * (c) 2019 Andrew McPherson 
 *
 * This library communicates with the Trill sensors
 * using I2C.
 *
 * BSD license
 */

#include "Trill.h"

Trill::Trill(uint8_t i2c_address, uint8_t reset_pin)
: i2c_address_(i2c_address), reset_pin_(reset_pin),
  device_type_(TRILL_DEVICE_NONE), firmware_version_(0),
  mode_(0xFF), last_read_loc_(0xFF), num_touches_(0),
  raw_bytes_left_(0)
{	
}

/* Initialise the hardware. Returns the type of device attached, or 0
   if none is attached. */
int Trill::begin() {
	/* De-assert reset if it is being used */
	if(reset_pin_ != 0) {
		pinMode(reset_pin_, OUTPUT);
		digitalWrite(reset_pin_, LOW);
	}
	
	/* Start I2C */
	Wire.begin();
	
	/* Put the device (assuming it exists) in normal mode */
	setMode(TRILL_MODE_NORMAL);
	
	/* Check the type of device attached */
	return identify();
}

/* Return the type of device attached, or 0 if none is attached. */
int Trill::identify() {
	Wire.beginTransmission(i2c_address_);
	Wire.write(kOffsetCommand);
	Wire.write(kCommandIdentify);
	Wire.endTransmission();
	
	last_read_loc_ = kOffsetCommand;
	Wire.requestFrom(i2c_address_, (uint8_t)2);
	
	if(Wire.available() < 2) {
		/* Unexpected or no response; no valid device connected */
		device_type_ = TRILL_DEVICE_NONE;
		firmware_version_ = 0;
		return device_type_;
	}
	
	device_type_ = Wire.read();
	firmware_version_ = Wire.read();
	
	return device_type_;
}

/* Read the latest scan value from the sensor. Returns true on success. */
boolean Trill::read() {
	uint8_t loc = 0;
	
	/* Set the read location to the right place if needed */
	prepareForDataRead();
	
	Wire.requestFrom(i2c_address_, (uint8_t)kNormalLengthDefault);
	while(Wire.available()) {
		buffer_[loc++] = Wire.read();
	}
	
	/* Check for read error */
	if(loc < kNormalLengthDefault) {
		num_touches_ = 0;
		return false;
	}
	
	/* Look for the first instance of 0xFFFF (no touch)
   	   in the buffer */
	for(loc = 0; loc < kMaxTouchNum; loc++) {
		if(buffer_[2 * loc] == 0xFF && buffer_[2 * loc + 1] == 0xFF)
			break;
	}
	num_touches_ = loc;
	
	return true;
}

/* Update the baseline value on the sensor */
void Trill::updateBaseline() {
	Wire.beginTransmission(i2c_address_);
	Wire.write(kOffsetCommand);
	Wire.write(kCommandBaselineUpdate);
	Wire.endTransmission();
	
	last_read_loc_ = kOffsetCommand;	
}

/* Reset the sensor, if reset pin is enabled */
void Trill::reset() {
	if(reset_pin_ == 0)
		return;
	/* Assert reset, hold for 10us, release */
	digitalWrite(reset_pin_, HIGH);
	delayMicroseconds(10);
	digitalWrite(reset_pin_, LOW);
}

/* How many touches? < 0 means error. */
int Trill::numberOfTouches() {
	if(mode_ != TRILL_MODE_NORMAL)
		return 0;
	return num_touches_;
}

/* Location and size of a particular touch, ranging from 0 to N-1.
   Returns -1 if no such touch exists. */
int Trill::touchLocation(uint8_t touch_num) {
	int result;

	if(mode_ != TRILL_MODE_NORMAL)
		return -1;	
	if(touch_num >= kMaxTouchNum)
		return -1;
	
	result = buffer_[2*touch_num] * 256;
	result += buffer_[2*touch_num + 1];
	
	return result;
}

int Trill::touchSize(uint8_t touch_num) {
	int result;

	if(mode_ != TRILL_MODE_NORMAL)
		return -1;	
	if(touch_num >= kMaxTouchNum)
		return -1;
	
	result = buffer_[2*touch_num + 2*kMaxTouchNum] * 256;
	result += buffer_[2*touch_num + 2*kMaxTouchNum + 1];
	
	return result;
}

/* Request raw data; wrappers for Wire */
void Trill::requestRawData(uint8_t max_length) {
	uint8_t length;
	
	prepareForDataRead();
	
	if(max_length == 0xFF) {
		/* Use default for the particular device if no length specified */
		if(device_type_ == TRILL_DEVICE_1D)
			length = kRawLength1D;
		else
			length = kRawLengthDefault;
	}
	if(length > kRawLengthMax)
		length = kRawLengthMax;
	
	/* The raw data might be longer than the Wire.h maximum buffer 
	 * (BUFFER_LENGTH in Wire.h).
	 * If so, split it into two reads. */
	if(length <= BUFFER_LENGTH) {
		Wire.requestFrom(i2c_address_, length);
		raw_bytes_left_ = 0;
	}
	else {
		Wire.requestFrom(i2c_address_, (uint8_t)BUFFER_LENGTH);
		raw_bytes_left_ = length - BUFFER_LENGTH;
	}
}

int Trill::rawDataAvailable() {
	/* Raw data items are 2 bytes long; return number of them available */
	return ((Wire.available() + raw_bytes_left_) >> 1);
}

/* Raw data is in 16-bit big-endian format */
int Trill::rawDataRead() {
	int result;
	
	if(Wire.available() < 2) {
		/* Read more bytes if we need it */
		if(raw_bytes_left_ > 0) {
			/* Move read pointer on device */
			Wire.beginTransmission(i2c_address_);
			Wire.write(kOffsetData + BUFFER_LENGTH);
			Wire.endTransmission();
			last_read_loc_ = kOffsetData + BUFFER_LENGTH;
			
			/* Now gather what's left */
			Wire.requestFrom(i2c_address_, raw_bytes_left_);
			raw_bytes_left_ = 0;		
		}

		/* Check again if we've got anything... */
		if(Wire.available() < 2)
			return 0;
	}
	
	result = (int)Wire.read() * 256;
	result += (int)Wire.read();
	return result;
}

/* Scan configuration settings */
void Trill::setMode(uint8_t mode) {
	Wire.beginTransmission(i2c_address_);
	Wire.write(kOffsetCommand);
	Wire.write(kCommandMode);
	Wire.write(mode);
	Wire.endTransmission();
	
	mode_ = mode;
	last_read_loc_ = kOffsetCommand;		
}

void Trill::setScanSettings(uint8_t speed, uint8_t num_bits) {
	Wire.beginTransmission(i2c_address_);
	Wire.write(kOffsetCommand);
	Wire.write(kCommandScanSettings);
	Wire.write(speed);
	Wire.write(num_bits);
	Wire.endTransmission();
	
	last_read_loc_ = kOffsetCommand;		
}

void Trill::setPrescaler(uint8_t prescaler) {
	Wire.beginTransmission(i2c_address_);
	Wire.write(kOffsetCommand);
	Wire.write(kCommandPrescaler);
	Wire.write(prescaler);
	Wire.endTransmission();
	
	last_read_loc_ = kOffsetCommand;			
}

void Trill::setNoiseThreshold(uint8_t threshold) {
	Wire.beginTransmission(i2c_address_);
	Wire.write(kOffsetCommand);
	Wire.write(kCommandNoiseThreshold);
	Wire.write(threshold);
	Wire.endTransmission();
	
	last_read_loc_ = kOffsetCommand;		
}

void Trill::setIDACValue(uint8_t value) {
	Wire.beginTransmission(i2c_address_);
	Wire.write(kOffsetCommand);
	Wire.write(kCommandIdac);
	Wire.write(value);
	Wire.endTransmission();
	
	last_read_loc_ = kOffsetCommand;		
}

void Trill::setMinimumTouchSize(uint16_t size) {
	Wire.beginTransmission(i2c_address_);
	Wire.write(kOffsetCommand);
	Wire.write(kCommandMinimumSize);
	Wire.write(size >> 8);
	Wire.write(size & 0xFF);
	Wire.endTransmission();
	
	last_read_loc_ = kOffsetCommand;		
}

/* Prepare the device to read data if it is not already prepared */
void Trill::prepareForDataRead() {
	if(last_read_loc_ != kOffsetData) {
		Wire.beginTransmission(i2c_address_);
		Wire.write(kOffsetData);
		Wire.endTransmission();

		last_read_loc_ = kOffsetData;
	}		
}
