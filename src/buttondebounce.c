// Based on code from:
// https://gitlab.com/qnx/projects/hardware-component-samples/-/blob/main/common/system/gpio/aarch64/rpi_gpio.h
// https://elahav.gitlab.io/qnx-rpi-book/realtime.html#event_loop.c-8
// https://pip-assets.raspberrypi.com/categories/545-raspberry-pi-4-model-b/documents/RP-008248-DS-1-bcm2711-peripherals.pdf

// To install this program and run on startup:
// After building and while running:
// ssh into Raspberry Pi
// cp /tmp/buttondebounce /data/home/qnxuser/bin/
// su -c vi /system/etc/startup/post_startup.sh
// add ./data/home/qnxuser/bin/buttondebounce to the end of the script

// To update the program after installing it:
// Before building:
// ssh into Raspberry Pi
// su -c slay buttondebounce
// After building and while running:
// cp /tmp/buttondebounce /data/home/qnxuser/bin/



#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <pthread.h>

#define GOOD_BUTTON 16
#define BAD_BUTTON 20

#define LCD_MOSI 10
#define LCD_SCLK 11
#define LCD_DC 22
#define RESET_PIN 27


static uint32_t volatile *gpio_regs;
static uint32_t volatile *spi_regs;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
uint32_t counter = 0;
uint32_t lcd_num_lookup[] = {
	//0
	0b01111110,
	0b10000001,
	0b10000001,
	0b10000001,
	0b10000001,
	0b10000001,
	0b01111110,
	0b00000000,
	//1
	0b10001000,
	0b10000100,
	0b10000010,
	0b11111111,
	0b10000000,
	0b10000000,
	0b10000000,
	0b00000000,
	//2
	0b10000100,
	0b11000010,
	0b10100001,
	0b10100001,
	0b10010001,
	0b10010010,
	0b10001100,
	0b00000000,
	//3
	0b01000010,
	0b10001001,
	0b10001001,
	0b10001001,
	0b10001001,
	0b10001001,
	0b01110110,
	0b00000000,
	//4
	0b00010000,
	0b00011000,
	0b00010100,
	0b00010010,
	0b11111111,
	0b00010000,
	0b00010000,
	0b00000000,
	//5
	0b01001111,
	0b10001001,
	0b10001001,
	0b10001001,
	0b10001001,
	0b10001001,
	0b01110001,
	0b00000000,
	//6
	0b01111110,
	0b10001001,
	0b10001001,
	0b10001001,
	0b10001001,
	0b10001001,
	0b01110001,
	0b00000000,
	//7
	0b10000001,
	0b01000001,
	0b00100001,
	0b00010001,
	0b00001001,
	0b00000101,
	0b00000011,
	0b00000000,
	//8
	0b01110110,
	0b10001001,
	0b10001001,
	0b10001001,
	0b10001001,
	0b10001001,
	0b01110110,
	0b00000000,
	//9
	0b01000110,
	0b10001001,
	0b10001001,
	0b10001001,
	0b10001001,
	0b10001001,
	0b01111110,
	0b00000000,
};


static inline void sendCommandToDisplay(uint32_t byte) {
	uint32_t temp;
	// Set D/C (pin 22) low
	// Write 0x00400000 to GPCLR0
	gpio_regs[10] = 0x00400000;
	// Send the byte
	spi_regs[1] = byte;
	// Poll until transfer is done
	while (1) {
		if (spi_regs[0] & 0x00010000) {
			// Read (and ignore) the received byte
			// This is necessary for the SPI registers to work correctly
			temp = spi_regs[1];
			return;
		}
	}
}

static inline void sendDataToDisplay(uint32_t byte) {
	uint32_t temp;
	// Set D/C (pin 22) high
	// Write 0x00400000 to GPSET0
	gpio_regs[7] = 0x00400000;
	// Send the byte
	spi_regs[1] = byte;
	// Poll until transfer is done
	while (1) {
		if (spi_regs[0] & 0x00010000) {
			// Read (and ignore) the received byte
			// This is necessary for the SPI registers to work correctly
			temp = spi_regs[1];
			return;
		}
	}
}


void display_counter_thread(void* arg) {
	uint32_t temp_counter; // Temporary copy of counter used for printing
	uint32_t i;
	uint32_t x_addr;

	// Set pins 10 and 11 as MOSI and SCLK
	// AND GPFSEL1 with 0xFFFFFFC0
	// OR GPFSEL1 with 0x00000024
	gpio_regs[1] &= 0xFFFFFFC0;
	gpio_regs[1] |= 0x00000024;

	// Set pins 22 and 27 as outputs
	// AND GPFSEL2 with 0xFF1FFE3F
	// OR GPFSEL2 with 0x00200040
	gpio_regs[2] &= 0xFF1FFE3F;
	gpio_regs[2] |= 0x00200040;

	// Set SPI to standard mode, no DMA, all chip select lines active low, transfer active
	// Set CS register to 0x00000080
	spi_regs[0] = 0x00000080;

	// Set clock divider to 512, SPI speed will be 3.5MHz (1.8GHz core clock / 512)
	// Set CLK register to 512
	spi_regs[2] = 512;

	// Send reset pulse to the LCD
	// Set LCD reset (pin 27) low
	// Write 0x08000000 to GPCLR0
	gpio_regs[10] = 0x08000000;
	usleep(100000); // Wait 100ms
	// Set LCD reset (pin 27) high
	// Write 0x08000000 to GPSET0
	gpio_regs[7] = 0x08000000;

	// Initialize LCD
	sendCommandToDisplay(0x21); // Extended instruction set
	sendCommandToDisplay(0xB1); // Set VOP
	sendCommandToDisplay(0x04); // Set temperature coefficient
	sendCommandToDisplay(0x14); // Set bias system
	sendCommandToDisplay(0x20); // Basic instruction set
	sendCommandToDisplay(0x0C); // Set display configuration

	// Set X,Y address to 0
	sendCommandToDisplay(0x80);
	sendCommandToDisplay(0x40);
	// Clear all pixels on display
	for (i = 0; i < 504; i++) {
		sendDataToDisplay(0);
	}
	sendCommandToDisplay(0x40);


	while (1) {
		// Quickly grab the value of the counter
		pthread_mutex_lock(&lock);
		temp_counter = counter;
		pthread_mutex_unlock(&lock);

		x_addr = 75;
		// Draw a 0 on the top right of the screen
		if (temp_counter == 0) {
			sendCommandToDisplay(0x80 + x_addr);
			sendDataToDisplay(lcd_num_lookup[0]);
			sendDataToDisplay(lcd_num_lookup[1]);
			sendDataToDisplay(lcd_num_lookup[2]);
			sendDataToDisplay(lcd_num_lookup[3]);
			sendDataToDisplay(lcd_num_lookup[4]);
			sendDataToDisplay(lcd_num_lookup[5]);
			sendDataToDisplay(lcd_num_lookup[6]);
			sendDataToDisplay(lcd_num_lookup[7]);
		}
		// Draw the value of the counter in base 10, drawing digits from right to left
		while (temp_counter > 0) {
			i = (temp_counter % 10) << 3;
			sendCommandToDisplay(0x80 + x_addr);
			sendDataToDisplay(lcd_num_lookup[i]);
			sendDataToDisplay(lcd_num_lookup[i+1]);
			sendDataToDisplay(lcd_num_lookup[i+2]);
			sendDataToDisplay(lcd_num_lookup[i+3]);
			sendDataToDisplay(lcd_num_lookup[i+4]);
			sendDataToDisplay(lcd_num_lookup[i+5]);
			sendDataToDisplay(lcd_num_lookup[i+6]);
			sendDataToDisplay(lcd_num_lookup[i+7]);
			temp_counter /= 10;
			x_addr -= 8;
		}
		// Wait 50ms (refreshes the screen at approximately) 20fps
		// This should probably be decreased to refresh the screen
		// at a higher FPS, but the pixel response time on that LCD
		// is so bad that it likely wouldn't make much of a difference
		usleep(50000);
	}
}


// Used as part of the debounce logic
void debounce_timer_handler() {
	if (~gpio_regs[13] & 0x00010000) {
		// Pin 16 is low
		// Start listening for rising edges on pin 16
		gpio_regs[31] |= 0x00010000;
	} else {
		// Pin 16 is high
		// Start listening for falling edges on pin 16
		gpio_regs[34] |= 0x00010000;
	}
}


// Resets the counter when the debounced button is held for 2 seconds
void reset_timer_handler() {
	if (~gpio_regs[13] & 0x00010000) {
		// Pin 16 is low
		// Start listening for rising edges on pin 16
		gpio_regs[31] |= 0x00010000;
		// Reset counter to 0
		pthread_mutex_lock(&lock);
		counter = 0;
		pthread_mutex_unlock(&lock);
		// Set X,Y address to 0
		sendCommandToDisplay(0x80);
		sendCommandToDisplay(0x40);
		// Clear all pixels on the first row of the display
		for (int i = 0; i < 84; i++) {
			sendDataToDisplay(0);
		}
		sendCommandToDisplay(0x40);
		return;
	}
}



int main(void) {
	int interrupt_id;
	timer_t debounce_timer_id;
	struct sigevent debounce_timer_event;
	struct itimerspec debounce_timer_info;
	timer_t reset_timer_id;
	struct sigevent reset_timer_event;
	struct itimerspec reset_timer_info;

	// Initialize GPIO registers
	gpio_regs = mmap(0, __PAGESIZE, PROT_READ | PROT_WRITE | PROT_NOCACHE, MAP_SHARED | MAP_PHYS, -1, 0xfe200000);
	if (gpio_regs == MAP_FAILED) {
		printf("Could not map GPIO registers!\n");
		return EXIT_FAILURE;
	}

	// Initialize SPI registers
	spi_regs = mmap(0, __PAGESIZE, PROT_READ | PROT_WRITE | PROT_NOCACHE, MAP_SHARED | MAP_PHYS, -1, 0xfe204000);
	if (spi_regs == MAP_FAILED) {
		printf("Could not map SPI registers!\n");
		return EXIT_FAILURE;
	}

	// The good (debounced) button will be on gpio pin 16
	// The bad (not debounced) button will be on gpio pin 20

	// Set pin 16 to input
	// AND GPFSEL1 with 0xFFE3FFFF
	gpio_regs[1] &= 0xFFE3FFFF;

	// Set pin 20 to input
	// AND GPFSEL2 with 0xFFFFFFF8
	gpio_regs[2] &= 0xFFFFFFF8;

	// Enable pull up resistors on pins 16 and 20
	// AND GPIO_PUP_PDN_CNTRL_REG1 with 0xFFFFFCFC
	// OR GPIO_PUP_PDN_CNTRL_REG1 with 0x00000202
	gpio_regs[58] &= 0xFFFFFCFC;
	gpio_regs[58] |= 0x00000101;

	// Clear all events
	gpio_regs[16] = 0xFFFFFFFF;
	gpio_regs[17] = 0xFFFFFFFF;
	gpio_regs[19] = 0;
	gpio_regs[20] = 0;
	gpio_regs[22] = 0;
	gpio_regs[23] = 0;
	gpio_regs[25] = 0;
	gpio_regs[26] = 0;
	gpio_regs[28] = 0;
	gpio_regs[29] = 0;

	// Enable falling edge detection on pins 16 and 20
	// OR GPAFEN0 with 0x00110000
	gpio_regs[34] |= 0x00110000;

	// Disable rising edge detection on pins 16 and 20
	// AND GPAREN0 with 0xFFEEFFFF
	gpio_regs[31] |= 0xFFEEFFFF;

	// Set up info for the debounce timer
	debounce_timer_event.sigev_notify = SIGEV_THREAD;
	debounce_timer_event.sigev_notify_function = &debounce_timer_handler;
	debounce_timer_event.sigev_notify_attributes = 0;
	debounce_timer_info.it_interval.tv_sec = 0;
	debounce_timer_info.it_interval.tv_nsec = 0;
	debounce_timer_info.it_value.tv_sec = 0;
	debounce_timer_info.it_value.tv_nsec = 25000000; // 25ms

	// Set up info about the reset timer
	reset_timer_event.sigev_notify = SIGEV_THREAD;
	reset_timer_event.sigev_notify_function = &reset_timer_handler;
	reset_timer_event.sigev_notify_attributes = 0;
	reset_timer_info.it_interval.tv_sec = 0;
	reset_timer_info.it_interval.tv_nsec = 0;
	reset_timer_info.it_value.tv_sec = 2; // 2 seconds
	reset_timer_info.it_value.tv_nsec = 0;

	// Create timers
	if (timer_create(CLOCK_REALTIME, &debounce_timer_event, &debounce_timer_id) == -1) {
		printf("Could not create timer!\n");
		return EXIT_FAILURE;
	}
	if (timer_create(CLOCK_REALTIME, &reset_timer_event, &reset_timer_id) == -1) {
		printf("Could not create timer!\n");
		return EXIT_FAILURE;
	}

	// Attach to the GPIO interrupt without unmasking
	interrupt_id = InterruptAttachThread(145, _NTO_INTR_FLAGS_NO_UNMASK);
	if (interrupt_id == -1) {
		printf("Could not attach interrupt!\n");
		return EXIT_FAILURE;
	}

	// Start running the display handling thread
	pthread_create(NULL, NULL, &display_counter_thread, NULL);

	// Main loop
	// All interrupts are handled here (for now)
	while (1) {
		if (InterruptWait(_NTO_INTR_WAIT_FLAGS_FAST |_NTO_INTR_WAIT_FLAGS_UNMASK, NULL) == -1) {
			printf("Error while waiting for interrupt!\n");
			return EXIT_FAILURE;
		}

		// Falling edge on good button
		if (gpio_regs[34] & ~gpio_regs[13] & 0x00010000) {
			// Stop listening for falling edges
			gpio_regs[34] &= 0xFFFEFFFF;
			// Increment counter
			pthread_mutex_lock(&lock);
			counter++;
			pthread_mutex_unlock(&lock);
			// Wait until timer expires to start listening for rising edges
			if (timer_settime(debounce_timer_id, 0, &debounce_timer_info, NULL) == -1) {
				printf("Could not set timer!\n");
				return EXIT_FAILURE;
			}
			if (timer_settime(reset_timer_id, 0, &reset_timer_info, NULL) == -1) {
				printf("Could not set timer!\n");
				return EXIT_FAILURE;
			}
		}
		// Rising edge on good button
		else if (gpio_regs[31] & gpio_regs[13] & 0x00010000) {
			// Stop listening for rising edges
			gpio_regs[31] &= 0xFFFEFFFF;
			// Wait until timer expires to start listening for falling edges
			if (timer_settime(debounce_timer_id, 0, &debounce_timer_info, NULL) == -1) {
				printf("Could not set timer!\n");
				return EXIT_FAILURE;
			}
		}

		// Falling edge on bad button
		if (gpio_regs[34] & ~gpio_regs[13] & 0x00100000) {
			// Stop listening for falling edges
			gpio_regs[34] &= 0xFFEFFFFF;
			// Increment counter
			pthread_mutex_lock(&lock);
			counter++;
			pthread_mutex_unlock(&lock);
			// Start listening for rising edges
			gpio_regs[31] |= 0x00100000;
		}
		// Rising edge on bad button
		else if (gpio_regs[31] & gpio_regs[13] & 0x00100000) {
			// Stop listening for rising edges
			gpio_regs[31] &= 0xFFEFFFFF;
			// Start listening for rising edges
			gpio_regs[34] |= 0x00100000;
		}

		// Clear detected events
		gpio_regs[16] = 0xFFFFFFFF;
		gpio_regs[17] = 0xFFFFFFFF;
	}

	return EXIT_SUCCESS;
}
