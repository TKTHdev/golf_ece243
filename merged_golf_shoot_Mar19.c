#include <math.h>
#include <stdlib.h>
#include <stdint.h>

/* Hardware Addresses */
#define HEX3_HEX0_BASE  0xFF200020  // 7-segment display HEX3 - HEX0
#define HEX5_HEX4_BASE  0xFF200030  // 7-segment display HEX5 and HEX4
#define TIMER_BASE      0xFF202000  // Timer
#define PS2_BASE        0xFF200100  // Keyboard
#define LED_BASE        0xFF200000  // LEDs
#define PIXEL_BUF_CTRL  0xFF203020  // Pixel buffer controller

/* Game Settings */
#define MAX_PLAYER      4           // Maximum number of players
#define BALL_SIZE       12          // Ball size
#define SCREEN_WIDTH    320         // Screen width
#define SCREEN_HEIGHT   240         // Screen height

/* 7-segment display patterns for digits */
const uint8_t SEVEN_SEG[10] = {
    0x3F, // 0: 0b00111111
    0x06, // 1: 0b00000110
    0x5B, // 2: 0b01011011
    0x4F, // 3: 0b01001111
    0x66, // 4: 0b01100110
    0x6D, // 5: 0b01101101
    0x7D, // 6: 0b01111101
    0x07, // 7: 0b00000111
    0x7F, // 8: 0b01111111
    0x67  // 9: 0b01100111
};

/* Ball structure */
typedef struct {
    int x;          // X position
    int y;          // Y position
    int radius;     // Radius
    int color;      // Color
    int dx;         // X velocity
    int dy;         // Y velocity
    int isActive;   // Active flag
    int momentum;   // Momentum (power)
} Ball;

/* Global variables */
// Double buffering
volatile int pixel_buffer_start;
short int Buffer1[240][512];  // Buffer 1: 240 rows x 512 columns (320 + padding)
short int Buffer2[240][512];  // Buffer 2

// Game state management
volatile int count = 1;             // Counter (1-100)
volatile int run = 1;               // Counter run flag
volatile int led0_on = 0;           // LED0 state (0=off, 1=on)
volatile int led1_on = 0;           // LED1 state (0=off, 1=on)
volatile int spacebar_pressed = 0;  // Spacebar state
volatile int break_code = 0;        // PS/2 break code flag

// Ball objects
Ball balls[MAX_PLAYER];

/* Function prototypes */
// Graphics functions
void clear_screen();
void plot_pixel(int x, int y, short int line_color);
void draw_line(int x0, int y0, int x1, int y1, short int line_color);
void draw_arrow(int center_x, int center_y, float cos_val, float sin_val, short int arrow_color);
void draw_ball(int x, int y, short int color);
int wait_for_vsync();

// Game mechanics
void move_ball(int player);
void shoot_the_ball(int player, int momentum, double angle);

// Hardware interface
void display_count(int value);
void led_update();
void config_ps2();
void config_timer();

// Interrupt handler
void __attribute__((interrupt)) interrupt_handler();

/**
 * Main function
 */
int main(void)
{
    // Get pixel buffer controller address
    volatile int *pixel_ctrl_ptr = (int *)PIXEL_BUF_CTRL;
    
    /* Initialize double buffering */
    // Set front pixel buffer to Buffer 1
    *(pixel_ctrl_ptr + 1) = (int)Buffer1;
    
    // Swap front/back buffers
    wait_for_vsync();
    
    // Initialize pixel buffer pointer
    pixel_buffer_start = *pixel_ctrl_ptr;
    clear_screen();
    
    // Set back pixel buffer to Buffer 2
    *(pixel_ctrl_ptr + 1) = (int)Buffer2;
    pixel_buffer_start = *(pixel_ctrl_ptr + 1);
    clear_screen();
    
    // Initialize direction and angle
    float cos_val = 1.0;
    float sin_val = 0.0;
    float angle = 0.0;
    float angle_increment = 0.05; // Angle change per frame

    // Initialize primary ball
    balls[0].x = 0;
    balls[0].y = 120;
    balls[0].color = 0x6666;
    balls[0].isActive = 1;
    balls[0].dx = 0;
    balls[0].dy = 0;

    /* Initialize hardware and interrupts */
    // Set up interrupt control registers
    unsigned int mstatus_value = 8;             // Enable global interrupts (mie bit = 1)
    unsigned int mie_value = 0x410000;          // Enable timer (bit 16) and PS2 (bit 22) interrupts
    unsigned int mtvec_value = (unsigned int)&interrupt_handler;

    // Reset display and LEDs
    volatile unsigned int* hex3_hex0 = (volatile unsigned int*)HEX3_HEX0_BASE;
    volatile unsigned int* hex5_hex4 = (volatile unsigned int*)HEX5_HEX4_BASE;
    volatile unsigned int* leds = (volatile unsigned int*)LED_BASE;
    *hex3_hex0 = 0; 
    *hex5_hex4 = 0;
    *leds = 0;
    
    // Configure hardware
    config_timer();
    config_ps2();
    
    // Set up interrupt registers using inline assembly
    __asm__ volatile ("csrw mstatus, %0" :: "r"(mstatus_value));
    __asm__ volatile ("csrw mie, %0" :: "r"(mie_value));
    __asm__ volatile ("csrw mtvec, %0" :: "r"(mtvec_value));

    // Display initial count
    display_count(count);
    
    /* Main game loop */
    while (1) {
        // Clear screen for new frame
        clear_screen();
        
        // Update angle based on arrow key input
        if (led0_on) {
            angle -= angle_increment;
        }

        if (led1_on) {
            angle += angle_increment;
        }

        // Handle spacebar input for shooting
        if (spacebar_pressed) {
            shoot_the_ball(0, count, angle);
        }

        // Reset angle when it completes a full circle
        if (angle >= 6.28) { // 2π radians
            angle = 0.0;
        }

        // Update and draw active balls
        for (int i = 0; i < MAX_PLAYER; i++) {
            if (balls[i].isActive == 1) {
                move_ball(i);
                draw_ball(balls[i].x, balls[i].y, balls[i].color);
            }
        }
        
        // Calculate direction vector components
        cos_val = cosf(angle);
        sin_val = sinf(angle);
        
        // Draw the direction arrow
        draw_arrow(0, 120, cos_val, sin_val, 0xF800); // Red arrow
        
        // Swap buffers and wait for VSync
        wait_for_vsync();
        pixel_buffer_start = *(pixel_ctrl_ptr + 1); // Update back buffer pointer
    }
}

/**
 * Draw an arrow based on direction vector
 * @param center_x Starting X position
 * @param center_y Starting Y position
 * @param cos_val Cosine of direction angle
 * @param sin_val Sine of direction angle
 * @param arrow_color Arrow color
 */
void draw_arrow(int center_x, int center_y, float cos_val, float sin_val, short int arrow_color)
{
    // Arrow length
    int arrow_length = 80;
    
    // Calculate arrow tip coordinates
    int tip_x = center_x + (int)(cos_val * arrow_length);
    int tip_y = center_y + (int)(sin_val * arrow_length);
    
    // Draw arrow shaft
    draw_line(center_x, center_y, tip_x, tip_y, arrow_color);
    
    // Draw arrowhead
    int arrowhead_length = 10;
    float angle1 = atan2f(sin_val, cos_val) + 2.5;  // ~150 degrees offset
    float angle2 = atan2f(sin_val, cos_val) - 2.5;  // ~-150 degrees offset
    
    int head1_x = tip_x + (int)(cosf(angle1) * arrowhead_length);
    int head1_y = tip_y + (int)(sinf(angle1) * arrowhead_length);
    int head2_x = tip_x + (int)(cosf(angle2) * arrowhead_length);
    int head2_y = tip_y + (int)(sinf(angle2) * arrowhead_length);
    
    draw_line(tip_x, tip_y, head1_x, head1_y, arrow_color);
    draw_line(tip_x, tip_y, head2_x, head2_y, arrow_color);
}

/**
 * Draw a filled ball
 * @param x X position
 * @param y Y position
 * @param color Ball color
 */
void draw_ball(int x, int y, short int color)
{
    //just draw the ball whose center is at (x,y) 
    //and the radius is BALL_SIZE
    for (int i = x - BALL_SIZE; i < x + BALL_SIZE; i++) {
        for (int j = y - BALL_SIZE; j < y + BALL_SIZE; j++) {
            if ((i - x) * (i - x) + (j - y) * (j - y) < BALL_SIZE * BALL_SIZE) {
                plot_pixel(i, j, color);
            }
        }
    }
}

/**
 * Wait for VSync to synchronize with VGA controller
 * @return 0 on success
 */
int wait_for_vsync()
{
    volatile int *pixel_ctrl_ptr = (int *)PIXEL_BUF_CTRL;
    
    // Write 1 to front buffer register to start synchronization
    *pixel_ctrl_ptr = 1;
    
    // Wait until status bit S becomes 0
    while ((*(pixel_ctrl_ptr + 3) & 0x1) != 0);
    
    return 0;
}

/**
 * Clear the screen by setting all pixels to black
 */
void clear_screen()
{
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            plot_pixel(x, y, 0x0000); // Black
        }
    }
}

/**
 * Draw a line using Bresenham's algorithm
 * @param x0 Starting X position
 * @param y0 Starting Y position
 * @param x1 Ending X position
 * @param y1 Ending Y position
 * @param line_color Line color
 */
void draw_line(int x0, int y0, int x1, int y1, short int line_color)
{
    // Determine if line is steep (greater y-difference than x-difference)
    int is_steep = (abs(y1 - y0) > abs(x1 - x0));
    
    // If steep, swap x and y coordinates
    if (is_steep) {
        int temp = x0;
        x0 = y0;
        y0 = temp;
        
        temp = x1;
        x1 = y1;
        y1 = temp;
    }
    
    // If line goes right to left, swap endpoints
    if (x0 > x1) {
        int temp = x0;
        x0 = x1;
        x1 = temp;
        
        temp = y0;
        y0 = y1;
        y1 = temp;
    }
    
    // Calculate deltas and error term
    int deltax = x1 - x0;
    int deltay = abs(y1 - y0);
    int error = -(deltax / 2);
    int y = y0;
    int y_step;
    
    // Determine y step direction
    y_step = (y0 < y1) ? 1 : -1;
    
    // Draw the line pixel by pixel
    for (int x = x0; x <= x1; x++) {
        if (is_steep) {
            plot_pixel(y, x, line_color);   // If steep, swap x and y
        } else {
            plot_pixel(x, y, line_color);   // Otherwise use normal coordinates
        }
        
        // Update error and y-position
        error = error + deltay;
        if (error > 0) {
            y = y + y_step;
            error = error - deltax;
        }
    }
}

/**
 * Plot a pixel at specified coordinates
 * @param x X position
 * @param y Y position
 * @param line_color Pixel color
 */
void plot_pixel(int x, int y, short int line_color)
{
    volatile short int *one_pixel_address;
    // Calculate pixel address in buffer
    one_pixel_address = pixel_buffer_start + (y << 10) + (x << 1);
    *one_pixel_address = line_color;
}

/**
 * Shoot a ball with specified momentum and angle
 * @param player Player ID
 * @param momentum Shot power
 * @param angle Shot direction angle
 */
void shoot_the_ball(int player, int momentum, double angle)
{
    balls[player].momentum = momentum;
    balls[player].dx = cos(angle) * 10;
    balls[player].dy = sin(angle) * 10;
}

/**
 * Update ball position and handle collisions
 * @param player Player ID
 */
void move_ball(int player)
{
    // Decrease momentum
    if (balls[player].momentum > 0) {
        balls[player].momentum--;
    } else {
        // Stop ball when momentum is depleted
        balls[player].dx = 0;
        balls[player].dy = 0;
    }
    
    // Update position
    balls[player].x += balls[player].dx;
    balls[player].y += balls[player].dy;
    
    // Handle boundary collisions with reflection
    if (balls[player].x < 0 || balls[player].x > SCREEN_WIDTH - BALL_SIZE) {
        balls[player].dx = -balls[player].dx;
    }
    if (balls[player].y < 0 || balls[player].y > SCREEN_HEIGHT - BALL_SIZE) {
        balls[player].dy = -balls[player].dy;
    }
}

/**
 * Display 3-digit count on 7-segment displays
 * @param value Value to display (1-100)
 */
void display_count(int value)
{
    volatile unsigned int* hex3_hex0 = (volatile unsigned int*)HEX3_HEX0_BASE;
    volatile unsigned int* hex5_hex4 = (volatile unsigned int*)HEX5_HEX4_BASE;
    
    // Extract individual digits
    int hundreds = value / 100;
    int tens = (value / 10) % 10;
    int ones = value % 10;
    
    // Build 32-bit value for HEX3-HEX0
    int hex3_hex0_value = 0;
    
    // Add hundreds digit (HEX2) if present
    if (hundreds > 0) {
        hex3_hex0_value |= (SEVEN_SEG[hundreds] << 16);
    }
    
    // Add tens digit (HEX1) if present
    if (tens > 0 || hundreds > 0) {
        hex3_hex0_value |= (SEVEN_SEG[tens] << 8);
    }
    
    // Add ones digit (HEX0)
    hex3_hex0_value |= SEVEN_SEG[ones];
    
    // Update displays
    *hex3_hex0 = hex3_hex0_value;
    *hex5_hex4 = 0; // Clear HEX5 and HEX4
}

/**
 * Update LED states based on global variables
 */
void led_update()
{
    volatile unsigned int* leds = (volatile unsigned int*)LED_BASE;
    int led_state = 0;
    
    // Set appropriate bits for active LEDs
    if (led0_on) led_state |= 1; // LED0
    if (led1_on) led_state |= 2; // LED1
    
    *leds = led_state;
}

/**
 * Configure PS/2 keyboard interface
 */
void config_ps2()
{
    volatile unsigned int* ps2 = (volatile unsigned int*)PS2_BASE;
    ps2[1] = 1; // Enable PS/2 interrupts (RE bit = 1)
}

/**
 * Configure timer with 0.05s interval
 */
void config_timer()
{
    volatile unsigned int* timer = (volatile unsigned int*)TIMER_BASE;
    int delay = 5000000;  // 0.05s at 100 MHz
    
    // Set counter start value
    timer[2] = delay & 0xFFFF;         // Low 16 bits
    timer[3] = (delay >> 16) & 0xFFFF; // High 16 bits
    
    // Set control register - START, CONT, and ITO bits
    timer[1] = 7; // 0b111
}

/**
 * Interrupt handler for timer and keyboard events
 */
void __attribute__((interrupt)) interrupt_handler()
{
    unsigned int mcause = 0;
    // Read mcause register to determine interrupt source
    __asm__ volatile ("csrr %0, mcause" : "=r"(mcause));
    mcause = mcause & 0x7FFFFFFF; // Mask MSB (interrupt vs exception bit)

    if (mcause == 16) { // Timer interrupt
        if (run) { // If counter is running
            count = count + 1;
            if (count > 100) count = 1;
            display_count(count);
        }
        
        // Clear timer interrupt flag
        volatile unsigned int* timer = (volatile unsigned int*)TIMER_BASE;
        timer[0] = 1;  // Set TO bit in status register
    }
    else if (mcause == 22) { // Keyboard interrupt
        volatile unsigned int* ps2 = (volatile unsigned int*)PS2_BASE;

        // Process all available keyboard data
        while (ps2[0] & 0x8000) { // While RVALID bit is set
            unsigned int ps2_data = ps2[0];
            unsigned int data = ps2_data & 0xFF; // Extract scan code

            if (data == 0xF0) { // Break code
                break_code = 1;  // Flag for key release
            }
            else if (data == 0xE0) { // Extended key prefix
                // Wait for following data byte
            }
            else {
                if (break_code) {  // Key release
                    if (data == 0x6B) {  // Left arrow released
                        led0_on = 0;
                    }
                    else if (data == 0x74) {  // Right arrow released
                        led1_on = 0;
                    }
                    else if (data == 0x29) { // Spacebar released
                        spacebar_pressed = 0;
                    }

                    break_code = 0;  // Reset break code flag
                }
                else {  // Key press
                    if (data == 0x29) { // Spacebar
                       run = 0;
                       spacebar_pressed = 1;
                    }
                    else if (data == 0x6B) { // Left arrow
                        led0_on = 1;
                    }
                    else if (data == 0x74) { // Right arrow
                        led1_on = 1;
                    }
                }
            }
        }
        
        // Update LED states
        led_update();
    }
}