#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

/* Hardware Addresses */
#define HEX3_HEX0_BASE  0xFF200020  // 7-segment display HEX3 - HEX0
#define HEX5_HEX4_BASE  0xFF200030  // 7-segment display HEX5 and HEX4
#define TIMER_BASE      0xFF202000  // Timer
#define PS2_BASE        0xFF200100  // Keyboard
#define LED_BASE        0xFF200000  // LEDs
#define PIXEL_BUF_CTRL  0xFF203020  // Pixel buffer controller
#define TIMER2_BASE     0xFF202020  // Timer 2

/* Game Settings */
#define COUNTDOWN_START 5
#define TIMER_X        (SCREEN_WIDTH - 20)  // Top right corner
#define TIMER_Y        10
#define ATTEMPTS_X     (SCREEN_WIDTH - 20)  // Bottom right corner
#define ATTEMPTS_Y     (SCREEN_HEIGHT - 20)
#define BALL_SIZE      4               // Ball radius
#define SCREEN_WIDTH   320             // Screen width
#define SCREEN_HEIGHT  240             // Screen height
#define LINE_NUM       2               // Number of lines in the course
#define PLAYER_NUM     1               // Number of players	


/*bool*/
typedef int bool;
#define true 1
#define false 0


/* 7-segment display patterns for digits */
const uint8_t SEVEN_SEG[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x67
};

/* Ball structure */
typedef struct {
    int x, y;         // Position
    int radius;       // Radius
    int color;        // Color
    int dx, dy;       // Velocity
    int isActive;     // Active flag
    int momentum;     // Power
} Ball;

/* Line structure */
typedef struct {
    int x0, y0, x1, y1;
    int isVertical;
} Line;

typedef struct {
    int x, y;
    int id;	
} Player;


/*Course structure*/
typedef struct {
    int goal_x, goal_y;
    Line lines[LINE_NUM];
} Course;


/* Global variables */
volatile int pixel_buffer_start;
short int Buffer1[240][512];  // Buffer 1
short int Buffer2[240][512];  // Buffer 2


volatile float cos_val = 1.0;
volatile float sin_val = 0.0;
volatile int count = 1;               // Counter (1-100)
volatile int run = 1;                 // Counter run flag
volatile int led0_on = 0;             // LED0 state
volatile int led1_on = 0;             // LED1 state
volatile int spacebar_pressed = 0;    // Spacebar state
volatile int break_code = 0;          // PS/2 break code flag
volatile int extended_code = 0;       // PS/2 extended code flag
volatile int button_used = 0;         // Button used flag
volatile float angle = 0.0;           // Current angle
volatile float angle_increment = 0.05; // Angle change per frame
volatile int countdown = COUNTDOWN_START; // Countdown timer
volatile int attempts = COUNTDOWN_START;  // Attempts remaining

int player_x = 0; // Player x position
int player_y = 120; // Player y position

volatile int count_pause = 0; // Pause counter for countdown timer

Ball balls[PLAYER_NUM];

/* Function prototypes */
void clear_screen();
void plot_pixel(int x, int y, short int line_color);
void draw_line(int x0, int y0, int x1, int y1, short int line_color);
void draw_arrow(int center_x, int center_y, float cos_val, float sin_val, short int arrow_color);
void draw_ball(int x, int y, short int color);
int wait_for_vsync();
void draw_digit(int x, int y, int digit, short int color);
void draw_number(int x, int y, int number, short int color);
void config_timer2();
void draw_attempts(int x, int y, int number, short int number_color, short int border_color);
void clear_attempts_area();
void clear_timer_area();
void move_ball(int player, Course *course);
void check_wall_collision(int player, Course *course);
void shoot_the_ball(int player, int momentum, double angle);
void display_count(int value);
void led_update();
void config_ps2();
void config_timer();
void __attribute__((interrupt)) interrupt_handler();
void generate_course();
void draw_course();
void clear_ps2_fifo();



Course course;

/* Main function */
int main(void) {
    volatile int * pixel_ctrl_ptr = (int *)PIXEL_BUF_CTRL;
    
    // Setup interrupt handling
    unsigned int mstatus_value = 8;  // MIE bit = 1
    unsigned int mie_value = 0x430000;  // Timer2 interrupt - IRQ 17, 16, and 22
    unsigned int mtvec_value = (unsigned int)&interrupt_handler;
    
    // Initialize double buffering
    *(pixel_ctrl_ptr + 1) = (int)Buffer1;
    wait_for_vsync();
    pixel_buffer_start = *pixel_ctrl_ptr;
    clear_screen();
    *(pixel_ctrl_ptr + 1) = (int)Buffer2;
    pixel_buffer_start = *(pixel_ctrl_ptr + 1);
    clear_screen();
    
    // Initialize ball
    for (int i = 0; i < PLAYER_NUM; i++) {
        balls[i].x = player_x;
        balls[i].y = player_y;
        balls[i].radius = BALL_SIZE;
        balls[i].color = 0x6666;
        balls[i].isActive = 0;
        balls[i].dx = 0;
        balls[i].dy = 0;
        balls[i].momentum = 0;
    }
    
    // Reset displays and LEDs
    volatile unsigned int* hex3_hex0 = (volatile unsigned int*)HEX3_HEX0_BASE;
    volatile unsigned int* hex5_hex4 = (volatile unsigned int*)HEX5_HEX4_BASE;
    volatile unsigned int* leds = (volatile unsigned int*)LED_BASE;
    *hex3_hex0 = 0; 
    *hex5_hex4 = 0;
    *leds = 0;
    
    // Configure hardware
    config_timer();
    config_timer2();
    config_ps2();
    
    // Set up interrupt registers
    __asm__ volatile ("csrw mstatus, %0" :: "r"(mstatus_value));
    __asm__ volatile ("csrw mie, %0" :: "r"(mie_value));
    __asm__ volatile ("csrw mtvec, %0" :: "r"(mtvec_value));
    
    display_count(count);

    // Generate course
    generate_course(&course,0);

    /* Main game loop */
    while (1) {
        clear_screen();

        // Draw course
        draw_course(&course);
        
        // Reset angle after full circle
        if (angle >= 6.28) {
            angle = 0.0;
        } else if (angle < 0) {
            angle += 6.28;
        }

        // Handle shooting - Modified logic for clarity
        if ((spacebar_pressed && !balls[0].isActive && !button_used) || (countdown == 0 && !balls[0].isActive)) {
            run = 0;
            shoot_the_ball(0, count, angle);
            spacebar_pressed = 0;
            button_used = 1;
            attempts--;
            count = 0;
            countdown = COUNTDOWN_START;
            count_pause = 1;
            // Don't update player position here - wait until ball stops
        }


        printf("momentum: %d\n", balls[0].momentum);

        // Update and draw active balls
        for (int i = 0; i < PLAYER_NUM; i++) {
            if (balls[i].isActive) {
                move_ball(i, &course);
                draw_ball(balls[i].x, balls[i].y, balls[i].color);
            }
        }
        
        // Update direction and draw arrow - only if no balls are active
        cos_val = cosf(angle);
        sin_val = sinf(angle);
        bool any_ball_active = false;
        for (int i = 0; i < PLAYER_NUM; i++) {
            if (balls[i].isActive) {
                any_ball_active = true;
                break;
            }
        }
        
        if (!any_ball_active) {
            draw_arrow(player_x, player_y, cos_val, sin_val, 0xF800);
        }
        
        // Update UI elements
        clear_timer_area();
        clear_attempts_area();
        draw_number(TIMER_X, TIMER_Y, countdown, 0xFFFF);
        draw_attempts(ATTEMPTS_X, ATTEMPTS_Y, attempts, 0xFFFF, 0x07E0);
        
        
        // Swap buffers
        wait_for_vsync();
        pixel_buffer_start = *(pixel_ctrl_ptr + 1);
        clear_ps2_fifo();

        printf("led0: %d, led1: %d\n", led0_on, led1_on);
    }
}

/* Timer 2 configuration */
void config_timer2() {
    volatile unsigned int* timer = (volatile unsigned int*)TIMER2_BASE;
    int delay = 100000000;  // 1s at 100 MHz
    timer[2] = delay & 0xFFFF;
    timer[3] = (delay >> 16) & 0xFFFF;
    timer[1] = 7;  // START, CONT, ITO
}

/* Function to clear the PS/2 keyboard FIFO */
void clear_ps2_fifo() {
    volatile unsigned int* ps2 = (volatile unsigned int*)PS2_BASE;
    
    // Read and discard all data in the FIFO until it's empty
    while (ps2[0] & 0x8000) {
        unsigned int dummy_read = ps2[0];
        // Just read to clear the buffer, no need to process
    }
}


/* Clear attempts display area */
void clear_attempts_area() {
    for (int x = ATTEMPTS_X - 9; x < ATTEMPTS_X + 25; x++) {
        for (int y = ATTEMPTS_Y - 9; y < ATTEMPTS_Y + 29; y++) {
            if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
                plot_pixel(x, y, 0x0000);
            }
        }
    }
}

/* Generate course */
void generate_course(Course* course, int course_id) {
    if (course_id==0)
    {
        // Line1
        course->lines[0].x0 = 0;
        course->lines[0].y0 = 100;
        course->lines[0].x1 = 320;
        course->lines[0].y1 = 100;
        course->lines[0].isVertical = 0;

        //Line 2
        course->lines[1].x0 = 0;
        course->lines[1].y0 = 200;
        course->lines[1].x1 = 320;
        course->lines[1].y1 = 200;
        course->lines[1].isVertical = 0;

        course->goal_x = 320;
        course->goal_y = 100;

    }
    else if(course_id = 2){}

}

/* Draw course */
void draw_course(Course *course) {
    for (int i = 0; i < LINE_NUM; i++) {
        draw_line(course->lines[i].x0, course->lines[i].y0, course->lines[i].x1, course->lines[i].y1, 0xFFFF);
    }
}

/* Draw attempts counter with border */
void draw_attempts(int x, int y, int number, short int number_color, short int border_color) {
    // Draw border rectangle
    draw_line(x - 9, y - 9, x + 14, y - 9, border_color);
    draw_line(x - 9, y + 18, x + 14, y + 18, border_color);
    draw_line(x - 9, y - 9, x - 9, y + 18, border_color);
    draw_line(x + 14, y - 9, x + 14, y + 18, border_color);
    
    // Draw number
    draw_number(x - 1, y - 1, number, number_color);
}

/* Draw direction arrow */
void draw_arrow(int center_x, int center_y, float cos_val, float sin_val, short int arrow_color) {
    int arrow_length = 20;  // Reduced length for better visualization
    int tip_x = center_x + (int)(cos_val * arrow_length);
    int tip_y = center_y + (int)(sin_val * arrow_length);
    
    // Draw shaft
    draw_line(center_x, center_y, tip_x, tip_y, arrow_color);
    
    // Draw arrowhead
    int arrowhead_length = 6;
    float angle1 = atan2f(sin_val, cos_val) + 2.5;
    float angle2 = atan2f(sin_val, cos_val) - 2.5;
    
    int head1_x = tip_x + (int)(cosf(angle1) * arrowhead_length);
    int head1_y = tip_y + (int)(sinf(angle1) * arrowhead_length);
    int head2_x = tip_x + (int)(cosf(angle2) * arrowhead_length);
    int head2_y = tip_y + (int)(sinf(angle2) * arrowhead_length);
    
    draw_line(tip_x, tip_y, head1_x, head1_y, arrow_color);
    draw_line(tip_x, tip_y, head2_x, head2_y, arrow_color);
}

/* Draw filled ball */
void draw_ball(int x, int y, short int color) {
    int radius = BALL_SIZE;
    
    int left = x - radius;
    int right = x + radius;
    int top = y - radius;
    int bottom = y + radius;
    
    // Clip to screen boundaries
    if (left < 0) left = 0;
    if (right >= SCREEN_WIDTH) right = SCREEN_WIDTH - 1;
    if (top < 0) top = 0;
    if (bottom >= SCREEN_HEIGHT) bottom = SCREEN_HEIGHT - 1;
    
    // Draw circle using distance check
    for (int i = left; i <= right; i++) {
        for (int j = top; j <= bottom; j++) {
            int dx = i - x;
            int dy = j - y;
            if (dx*dx + dy*dy <= radius*radius) {
                plot_pixel(i, j, color);
            }
        }
    }
}

/* Sync with VGA controller */
int wait_for_vsync() {
    volatile int *pixel_ctrl_ptr = (int *)PIXEL_BUF_CTRL;
    *pixel_ctrl_ptr = 1;
    while ((*(pixel_ctrl_ptr + 3) & 0x1) != 0);
    return 0;
}

/* Clear screen */
void clear_screen() {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            plot_pixel(x, y, 0x0000);
        }
    }
}

/* Draw line using Bresenham's algorithm */
void draw_line(int x0, int y0, int x1, int y1, short int line_color) {
    int is_steep = (abs(y1 - y0) > abs(x1 - x0));
    
    if (is_steep) {
        int temp = x0; x0 = y0; y0 = temp;
        temp = x1; x1 = y1; y1 = temp;
    }
    
    if (x0 > x1) {
        int temp = x0; x0 = x1; x1 = temp;
        temp = y0; y0 = y1; y1 = temp;
    }
    
    int deltax = x1 - x0;
    int deltay = abs(y1 - y0);
    int error = -(deltax / 2);
    int y = y0;
    int y_step = (y0 < y1) ? 1 : -1;
    
    for (int x = x0; x <= x1; x++) {
        if (is_steep) {
            plot_pixel(y, x, line_color);
        } else {
            plot_pixel(x, y, line_color);
        }
        
        error += deltay;
        if (error > 0) {
            y += y_step;
            error -= deltax;
        }
    }
}

/* Plot pixel with bounds checking */
void plot_pixel(int x, int y, short int line_color) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        volatile short int *pixel_addr = (volatile short int *)(pixel_buffer_start + (y << 10) + (x << 1));
        *pixel_addr = line_color;
    }
}

/* Clear timer display area */
void clear_timer_area() {
    for (int x = TIMER_X - 2; x < TIMER_X + 12; x++) {
        for (int y = TIMER_Y - 2; y < TIMER_Y + 14; y++) {
            if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
                plot_pixel(x, y, 0x0000);
            }
        }
    }
}

/* Shoot ball with given momentum and angle */
void shoot_the_ball(int player, int momentum, double angle) {
    if (player < 0 || player >= PLAYER_NUM) return;
   
    
    //Clear the ps/2 keyboard fifo to prevent buffered key presses
    clear_ps2_fifo();


    // Calculate velocity components
    float speed_factor = 2.0 + (momentum / 20.0);
    balls[player].dx = cosf(angle) * speed_factor;
    balls[player].dy = sinf(angle) * speed_factor;
    balls[player].momentum = momentum/2;
    balls[player].isActive = 1;
    
    // Use current player position as starting point
    balls[player].x = player_x;
    balls[player].y = player_y;
}

/* Update ball position and handle collisions */
void move_ball(int player, Course *course) {
    if (player < 0 || player >= PLAYER_NUM || !balls[player].isActive) return;

    // If momentum depleted, stop ball
    if (balls[player].momentum <= 0) {
        balls[player].dx = 0;
        balls[player].dy = 0;
        balls[player].isActive = 0;
        button_used = 0;  // Allow new shots
        
        // Update player position to match the ball's final position
        player_x = balls[player].x;
        player_y = balls[player].y;
        
        // Reset count_pause to allow countdown timer to continue
        count_pause = 0;

        run = 1;
        
        return;
    }

    balls[player].momentum--;

    // Calculate new position
    int old_x = balls[player].x;
    int old_y = balls[player].y;
    int new_x = old_x + (int)balls[player].dx;
    int new_y = old_y + (int)balls[player].dy;
    
    // Handle screen boundary collisions with proper bounce physics
    if (new_x < BALL_SIZE) {
        new_x = BALL_SIZE;
        balls[player].dx = -balls[player].dx * 0.8f;  // Some energy loss on collision
    }
    else if (new_x > SCREEN_WIDTH - BALL_SIZE) {
        new_x = SCREEN_WIDTH - BALL_SIZE;
        balls[player].dx = -balls[player].dx * 0.8f;
    }
    
    if (new_y < BALL_SIZE) {
        new_y = BALL_SIZE;
        balls[player].dy = -balls[player].dy * 0.8f;
    }
    else if (new_y > SCREEN_HEIGHT - BALL_SIZE) {
        new_y = SCREEN_HEIGHT - BALL_SIZE;
        balls[player].dy = -balls[player].dy * 0.8f;
    }
    
    // Update position
    balls[player].x = new_x;
    balls[player].y = new_y;

    // Check for wall collision
    check_wall_collision(player, course);
}

/* Check if the moving ball hit the wall, and if so, bounce */
void check_wall_collision(int player, Course* course) {
    if (player < 0 || player >= PLAYER_NUM || !balls[player].isActive) return;
    
    float collision_margin = 0.5f; // Add a small margin to avoid getting stuck
    
    // Check if the ball hit any of the course walls
    for (int i = 0; i < LINE_NUM; i++) {
        if (course->lines[i].isVertical) {
            // Collision with vertical wall
            if (balls[player].x + balls[player].radius >= course->lines[i].x0 - collision_margin && 
                balls[player].x - balls[player].radius <= course->lines[i].x0 + collision_margin) {
                
                // Check if ball is within the vertical range of the line
                if (balls[player].y + balls[player].radius >= course->lines[i].y0 && 
                   balls[player].y - balls[player].radius <= course->lines[i].y1) {
                    
                    // Move the ball away from the wall to prevent sticking
                    if (balls[player].dx > 0) {
                        balls[player].x = course->lines[i].x0 - balls[player].radius - collision_margin;
                    } else {
                        balls[player].x = course->lines[i].x0 + balls[player].radius + collision_margin;
                    }
                    
                    // Reverse horizontal velocity with energy loss
                    balls[player].dx = -balls[player].dx * 0.8f;
                }
            }
        }
        else {
            // Collision with horizontal wall
            if (balls[player].y + balls[player].radius >= course->lines[i].y0 - collision_margin && 
               balls[player].y - balls[player].radius <= course->lines[i].y0 + collision_margin) {
                
                // Check if ball is within the horizontal range of the line
                if (balls[player].x + balls[player].radius >= course->lines[i].x0 && 
                   balls[player].x - balls[player].radius <= course->lines[i].x1) {
                    
                    // Move the ball away from the wall to prevent sticking
                    if (balls[player].dy > 0) {
                        balls[player].y = course->lines[i].y0 - balls[player].radius - collision_margin;
                    } else {
                        balls[player].y = course->lines[i].y0 + balls[player].radius + collision_margin;
                    }
                    
                    // Reverse vertical velocity with energy loss
                    balls[player].dy = -balls[player].dy * 0.8f;
                }
            }
        }
    }
}

/* Display count on 7-segment displays */
void display_count(int value) {
    volatile unsigned int* hex3_hex0 = (volatile unsigned int*)HEX3_HEX0_BASE;
    volatile unsigned int* hex5_hex4 = (volatile unsigned int*)HEX5_HEX4_BASE;
    
    int hundreds = value / 100;
    int tens = (value / 10) % 10;
    int ones = value % 10;
    
    int hex3_hex0_value = 0;
    
    if (hundreds > 0) {
        hex3_hex0_value |= (SEVEN_SEG[hundreds] << 16);
    }
    
    if (tens > 0 || hundreds > 0) {
        hex3_hex0_value |= (SEVEN_SEG[tens] << 8);
    }
    
    hex3_hex0_value |= SEVEN_SEG[ones];
    
    *hex3_hex0 = hex3_hex0_value;
    *hex5_hex4 = 0;
}

/* Update LED states */
void led_update() {
    volatile unsigned int* leds = (volatile unsigned int*)LED_BASE;
    int led_state = 0;
    
    if (led0_on) led_state |= 1;
    if (led1_on) led_state |= 2;
    
    *leds = led_state;
}

/* Configure PS/2 keyboard */
void config_ps2() {
    volatile unsigned int* ps2 = (volatile unsigned int*)PS2_BASE;
    ps2[1] = 1; // Enable interrupts
}

/* Configure timer */
void config_timer() {
    volatile unsigned int* timer = (volatile unsigned int*)TIMER_BASE;
    int delay = 5000000;  // 0.05s at 100 MHz
    
    timer[2] = delay & 0xFFFF;
    timer[3] = (delay >> 16) & 0xFFFF;
    timer[1] = 7; // START, CONT, ITO
}

/* Interrupt handler */
void __attribute__((interrupt)) interrupt_handler() {
    unsigned int mcause = 0;
    __asm__ volatile ("csrr %0, mcause" : "=r"(mcause));
    mcause = mcause & 0x7FFFFFFF;

    if (mcause == 17) { // Timer2 interrupt - countdown timer
        if (!count_pause && countdown > 0) {
            countdown--;
        }
        volatile unsigned int* timer = (volatile unsigned int*)TIMER2_BASE;
        timer[0] = 1;  // Clear interrupt
    }
    else if (mcause == 16) { // Timer interrupt - for power counter
        if (run) {
            count = count + 1;
            if (count > 100) count = 1;
            display_count(count);
        }
        
        // Handle arrow rotation
        if (led0_on) {
            angle -= angle_increment;
            if (angle < 0) {
                angle += 6.28;
            }
        }
        
        if (led1_on) {
            angle += angle_increment;
            if (angle >= 6.28) {
                angle = 0.0;
            }
        }
        
        // Clear interrupt
        volatile unsigned int* timer = (volatile unsigned int*)TIMER_BASE;
        timer[0] = 1;
    }
    else if (mcause == 22) { // Keyboard interrupt
        volatile unsigned int* ps2 = (volatile unsigned int*)PS2_BASE;
        
        // Process all available data
        while (ps2[0] & 0x8000) {
            unsigned int ps2_data = ps2[0];
            unsigned int data = ps2_data & 0xFF;
            
            if (data == 0xF0) { // Break code
                break_code = 1;
            }
            else if (data == 0xE0) { // Extended key prefix
                extended_code = 1;
            }
            else {
                if (break_code) {  // Key release
                    if (data == 0x6B) {  // Left arrow
                        led0_on = 0;
                    }
                    else if (data == 0x74) {  // Right arrow
                        led1_on = 0;
                    }
                    else if (data == 0x29) { // Spacebar
                        spacebar_pressed = 0;
                    }
                    break_code = 0;
                    extended_code = 0;  // Also reset extended code
                }
                else {  // Key press
                    if (data == 0x29 && !button_used && !balls[0].isActive) { // Spacebar
                        spacebar_pressed = 1;
                    }
                    else if (data == 0x6B) { // Left arrow
                        led0_on = 1;
                    }
                    else if (data == 0x74) { // Right arrow
                        led1_on = 1;
                    }
                    
                    // Process extended keys here if needed
                    if (extended_code) {
                        // Handle extended key codes if necessary
                        extended_code = 0;
                    }
                }
            }
        }
        
        led_update();
    }
}

/* Draw single digit */
void draw_digit(int x, int y, int digit, short int color) {
    switch(digit) {
        case 5:
            draw_line(x, y, x+8, y, color);
            draw_line(x, y, x, y+6, color);
            draw_line(x, y+6, x+8, y+6, color);
            draw_line(x+8, y+6, x+8, y+12, color);
            draw_line(x, y+12, x+8, y+12, color);
            break;
        case 4:
            draw_line(x, y, x, y+6, color);
            draw_line(x, y+6, x+8, y+6, color);
            draw_line(x+8, y, x+8, y+12, color);
            break;
        case 3:
            draw_line(x, y, x+8, y, color);
            draw_line(x+8, y, x+8, y+12, color);
            draw_line(x, y+6, x+8, y+6, color);
            draw_line(x, y+12, x+8, y+12, color);
            break;
        case 2:
            draw_line(x, y, x+8, y, color);
            draw_line(x+8, y, x+8, y+6, color);
            draw_line(x, y+6, x+8, y+6, color);
            draw_line(x, y+6, x, y+12, color);
            draw_line(x, y+12, x+8, y+12, color);
            break;
        case 1:
            draw_line(x+4, y, x+4, y+12, color);
            break;
        case 0:
            draw_line(x, y, x+8, y, color);
            draw_line(x, y, x, y+12, color);
            draw_line(x+8, y, x+8, y+12, color);
            draw_line(x, y+12, x+8, y+12, color);
            break;
    }
}

/* Draw number using digits */
void draw_number(int x, int y, int number, short int color) {
    if (number >= 0 && number <= 5) {
        draw_digit(x, y, number, color);
    }
}