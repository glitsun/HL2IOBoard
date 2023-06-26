// This is firmware for the Hermes Lite 2 IO board designed by Jim Ahlstrom, N2ADR. It is
//   Copyright (c) 2022-2023 James C. Ahlstrom <jahlstr@gmail.com>.
//   It is licensed under the MIT license. See MIT.txt.

// This is an interrupt service routine for I2C traffic. It must return quickly.

#include "../hl2ioboard.h"

uint8_t Registers[256];		// copy of registers written to the Pico except for read-only registers
irq_handler IrqHandler[256];	// call these handlers (if any) after a register is written

uint64_t new_tx_freq;

void i2c_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event)
{  // Receive and send I2C traffic. This is an interrupt service routine so return quickly!
	static uint8_t i2c_regs_control;		// the control (register) byte for receive or request
	static uint8_t i2c_control_valid = false;	// is i2c_regs_control valid?
	uint8_t data;

	switch (event) {
	case I2C_SLAVE_RECEIVE: // master has written data and this slave receives it
		data = i2c_read_byte_raw(i2c);
		if ( ! i2c_control_valid) {	// the first byte is the control (register number)
			i2c_regs_control = data;
			i2c_control_valid = true;
			fast_led_flasher();
		}
		else {
			if (i2c_regs_control < 4 || i2c_regs_control > 10)
				Registers[i2c_regs_control] = data;	// registers 4 to 10 are read only
			switch (i2c_regs_control) {
			case REG_RF_INPUTS:		// How to use the external Rx input at J9
				switch (data) {
				case 0:		// Normal HL2 Rx input, J9 not used, Pure Signal at J10 available
					gpio_put(GPIO03_INTTR, 0);
					gpio_put(GPIO02_RF3, 0);
					break;
				case 1:		// Use J9 for Rx input, Pure Signal at J10 is not available
					gpio_put(GPIO03_INTTR, 1);
					gpio_put(GPIO02_RF3, 1);
					break;
				case 2:		// Use J9 for Rx input on Rx, use Pure Signal at J10 for Tx
					gpio_put(GPIO03_INTTR, 1);
					IrqRxTxChange(GPIO13_EXTTR, 0xc);
					break;
				}
				break;
			case REG_FAN_SPEED:		// fan control
				pwm_set_chan_level(FAN_SLICE, FAN_CHAN, FAN_WRAP * data / 255);
				break;
			case REG_TX_FREQUENCY:		// Tx frequency, LSB
				new_tx_freq = (uint64_t)data	// Thanks to Neil, G4BRK
					| (uint64_t)Registers[3] << 8
					| (uint64_t)Registers[2] << 16
					| (uint64_t)Registers[1] << 24
					| (uint64_t)Registers[0] << 32;
				if (new_tx_freq <= 2500000)
					gpio_put(GPIO00_HPF, 0);
				else
					gpio_put(GPIO00_HPF, 1);
				break;
			}
			if (IrqHandler[i2c_regs_control])
				(IrqHandler[i2c_regs_control])(i2c_regs_control, data);
		}
		break;
	case I2C_SLAVE_REQUEST: // master is requesting data
		switch (i2c_regs_control) {
		case REG_FIRMWARE_MAJOR:
			data = firmware_version_major;
			break;
		case REG_FIRMWARE_MINOR:
			data = firmware_version_minor;
			break;
		case REG_INPUT_PINS:			// return the state of the input pins
			data = 0;
			if (gpio_get(GPIO06_In5))
				data |= 1 << 5;
			if (gpio_get(GPIO07_In4))
				data |= 1 << 4;
			if (gpio_get(GPIO21_In3))
				data |= 1 << 3;
			if (gpio_get(GPIO18_In2))
				data |= 1 << 2;
			if (gpio_get(GPIO17_In1))
				data |= 1 << 1;
			if (gpio_get(GPIO13_EXTTR))	// 1 for receive, 0 for transmit
				data |= 1;
			break;
		default:
			data = Registers[i2c_regs_control];
			break;
		}
		i2c_write_byte_raw(i2c, data);
		i2c_regs_control++;
		break;
	case I2C_SLAVE_FINISH: // master has signalled Stop or Restart
		i2c_control_valid = false;
		break;
	}
}

void IrqRxTxChange(uint gpio, uint32_t events)
{  // Called when EXTTR changes
	if (Registers[REG_RF_INPUTS] == 2) {
		if (gpio_get(GPIO13_EXTTR))	// Rx
			gpio_put(GPIO02_RF3, 1);
		else				// Tx
			gpio_put(GPIO02_RF3, 0);
	}
}
