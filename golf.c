#include <math.h>
#include <stdlib.h>

#define MAX_PLAYER 4
#define BALL_SIZE 4
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// Global variables for double buffering
volatile int pixel_buffer_start; // global variable
short int Buffer1[240][512]; // 240 rows, 512 (320 + padding) columns
short int Buffer2[240][512];

typedef struct Ball {
    int x;
    int y;
    int radius;
    int color;
    int dx;
    int dy;
    int isActive;
    int momentum;
} Ball;

Ball balls[MAX_PLAYER];

/* Function prototypes */
void clear_screen();
void draw_line(int x0, int y0, int x1, int y1, short int line_color);
void plot_pixel(int x, int y, short int line_color);
void draw_arrow(int center_x, int center_y, float cos_val, float sin_val, short int arrow_color);
void draw_ball(int x, int y, short int color);
void shoot_the_ball(int player, int momentum, double angle);
int wait_for_vsync();

int main(void)
{
    // Get the address of the pixel buffer controller
    volatile int * pixel_ctrl_ptr = (int *)0xFF203020;
    
    /* set front pixel buffer to Buffer 1 */
    *(pixel_ctrl_ptr + 1) = (int)Buffer1; // first store the address in the back buffer
    
    /* now, swap the front/back buffers, to set the front buffer location */
    wait_for_vsync();
    
    /* initialize a pointer to the pixel buffer, used by drawing functions */
    pixel_buffer_start = *pixel_ctrl_ptr;
    clear_screen(); // pixel_buffer_start points to the pixel buffer
    
    /* set back pixel buffer to Buffer 2 */
    *(pixel_ctrl_ptr + 1) = (int)Buffer2;
    pixel_buffer_start = *(pixel_ctrl_ptr + 1); // we draw on the back buffer
    clear_screen(); // pixel_buffer_start points to the pixel buffer
    
    // Initial direction settings (pointing right - cos=1, sin=0)
    float cos_val = 1.0;
    float sin_val = 0.0;
    float angle = 0.0;
    float angle_increment = 0.05; // Angle increment per frame

    // Initialize ball
    balls[0].x = 160;
    balls[0].y = 120;
    balls[0].color = 0x6666;
    balls[0].isActive = 1;
    balls[0].dx = 0;
    balls[0].dy = 0;


    shoot_the_ball(0, 100, 0.0); 
    
    // Main loop
    while (1) {
        // Clear the screen for each new frame
        clear_screen();
        
        // Update the angle
        angle += angle_increment;
        if (angle >= 6.28) { // Reset when reaches 2π (full circle)
            angle = 0.0;
        }

        // Draw active balls
        for(int i = 0; i < MAX_PLAYER; i++){
            if (balls[i].isActive == 1){
                move_ball(i);
                draw_ball(balls[i].x, balls[i].y, balls[i].color);
            }
        }
        
        // Calculate new cosine and sine values
        cos_val = cosf(angle);
        sin_val = sinf(angle);
        
        // Draw the arrow in the specified direction
        draw_arrow(160, 120, cos_val, sin_val, 0xF800); // Draw arrow in red color
        
        // Synchronize with the VGA controller
        wait_for_vsync(); // swap front and back buffers on VGA vertical sync
        pixel_buffer_start = *(pixel_ctrl_ptr + 1); // new back buffer
    }
}

/* Function to draw an arrow based on the given starting point, cosine and sine values */
void draw_arrow(int center_x, int center_y, float cos_val, float sin_val, short int arrow_color)
{
    // Arrow length
    int arrow_length = 80;
    
    // Calculate the coordinates of the arrow tip
    int tip_x = center_x + (int)(cos_val * arrow_length);
    int tip_y = center_y + (int)(sin_val * arrow_length);
    
    // Draw the arrow body (line from center to tip)
    draw_line(center_x, center_y, tip_x, tip_y, arrow_color);
    
    // 矢印の先端（三角形）を追加
    int arrowhead_length = 10;
    float angle1 = atan2f(sin_val, cos_val) + 2.5;  // 約150度
    float angle2 = atan2f(sin_val, cos_val) - 2.5;  // 約-150度
    
    int head1_x = tip_x + (int)(cosf(angle1) * arrowhead_length);
    int head1_y = tip_y + (int)(sinf(angle1) * arrowhead_length);
    int head2_x = tip_x + (int)(cosf(angle2) * arrowhead_length);
    int head2_y = tip_y + (int)(sinf(angle2) * arrowhead_length);
    
    draw_line(tip_x, tip_y, head1_x, head1_y, arrow_color);
    draw_line(tip_x, tip_y, head2_x, head2_y, arrow_color);
}

/* Draw a filled ball at position (x,y) with the specified color */
void draw_ball(int x, int y, short int color)
{
    for (int i = 0; i < BALL_SIZE; i++) {
        for (int j = 0; j < BALL_SIZE; j++) {
            plot_pixel(x + i, y + j, color);
        }
    }
}

/* Wait for vsync to synchronize with the VGA controller */
int wait_for_vsync()
{
    volatile int * pixel_ctrl_ptr = (int *)0xFF203020;
    
    // Write 1 to the front buffer register to initiate the synchronization
    *pixel_ctrl_ptr = 1;
    
    // Wait until bit S of the Status register becomes 0
    while ((*(pixel_ctrl_ptr + 3) & 0x1) != 0);
    
    return 0;
}

/* Clear the screen by setting all pixels to black (0x0000) */
void clear_screen()
{
    int x, y;
    // Iterate through all pixels on screen (320x240)
    for (x = 0; x < SCREEN_WIDTH; x++)
        for (y = 0; y < SCREEN_HEIGHT; y++)
            plot_pixel(x, y, 0x0000); // Set each pixel to black
}

/* Implementation of Bresenham's line-drawing algorithm */
void draw_line(int x0, int y0, int x1, int y1, short int line_color)
{
    // Determine if the line is steep (greater y-difference than x-difference)
    int is_steep = (abs(y1 - y0) > abs(x1 - x0));
    
    // If the line is steep, swap x and y coordinates
    if (is_steep) {
        // Swap x0 and y0
        int temp = x0;
        x0 = y0;
        y0 = temp;
        
        // Swap x1 and y1
        temp = x1;
        x1 = y1;
        y1 = temp;
    }
    
    // If line goes from right to left, swap endpoints
    if (x0 > x1) {
        // Swap x0 and x1
        int temp = x0;
        x0 = x1;
        x1 = temp;
        
        // Swap y0 and y1
        temp = y0;
        y0 = y1;
        y1 = temp;
    }
    
    // Calculate deltas and error
    int deltax = x1 - x0;
    int deltay = abs(y1 - y0);
    int error = -(deltax / 2);
    int y = y0;
    int y_step;
    
    // Determine whether to increment or decrement y
    if (y0 < y1)
        y_step = 1;    // Moving down
    else
        y_step = -1;   // Moving up
    
    // Draw the line pixel by pixel
    for (int x = x0; x <= x1; x++) {
        if (is_steep)
            plot_pixel(y, x, line_color);   // If steep, swap x and y
        else
            plot_pixel(x, y, line_color);   // Otherwise, use normal coordinates
        
        // Update error and y-position
        error = error + deltay;
        if (error > 0) {
            y = y + y_step;
            error = error - deltax;
        }
    }
}

/* Function to set a specific pixel to the given color */
void plot_pixel(int x, int y, short int line_color)
{
    // Modified to work with the 2D array buffer format
    volatile short int *one_pixel_address;
    one_pixel_address = pixel_buffer_start + (y << 10) + (x << 1);
    *one_pixel_address = line_color;
}

// Take player id and momentum as input
// Shoot the ball in the direction of the arrow
// Momentum is the power of the shot
// Momentum is the distance the ball will travel
void shoot_the_ball(int player, int momentum, double angle){
    balls[player].momentum = momentum;
    balls[player].dx = cos(angle);
    balls[player].dy = sin(angle);
}

void move_ball(int player){

    if (balls[player].momentum > 0){
        balls[player].momentum--;
    }
    else{
        balls[player].dx = 0;
        balls[player].dy = 0;
    }
    balls[player].x += balls[player].dx;
    balls[player].y += balls[player].dy;
    //reflect 
    if (balls[player].x < 0 || balls[player].x > SCREEN_WIDTH){
        balls[player].dx = -balls[player].dx;
    }
    if (balls[player].y < 0 || balls[player].y > SCREEN_HEIGHT){
        balls[player].dy = -balls[player].dy;
    }
}