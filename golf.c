#include <math.h>
#include <stdlib.h>

int pixel_buffer_start; 

/* Function prototypes */
void clear_screen();
void draw_line(int x0, int y0, int x1, int y1, short int line_color);
void plot_pixel(int x, int y, short int line_color);
void draw_arrow(int center_x,int center_y, float cos_val, float sin_val, short int arrow_color);
int wait_for_vsync();

int main(void)
{
    // Get the address of the pixel buffer controller
    volatile int * pixel_ctrl_ptr = (int *)0xFF203020;
    
    /* Read location of the pixel buffer from the pixel buffer controller */
    pixel_buffer_start = *pixel_ctrl_ptr;
    
    // Clear the screen initially
    clear_screen();
    
    // Initial direction settings (pointing right - cos=1, sin=0)
    float cos_val = 1.0;
    float sin_val = 0.0;
    float angle = 0.0;
    float angle_increment = 0.05; // Angle increment per frame
    
    // Main loop
    while (1) {
        // Clear the screen for each new frame
        clear_screen();
        
        // Update the angle
        angle += angle_increment;
        if (angle >= 6.28) { // Reset when reaches 2π (full circle)
            angle = 0.0;
        }
        
        // Calculate new cosine and sine values
        cos_val = cosf(angle);
        sin_val = sinf(angle);
        
        // Draw the arrow in the specified direction
        draw_arrow(160,120,cos_val, sin_val, 0xF800); // Draw arrow in red color
        
        // Synchronize with the VGA controller
        wait_for_vsync();
    }
}

/* Function to draw an arrow based on the given starting point, cosine and sine values */
void draw_arrow(int center_x, int center_y, float cos_val, float sin_val, short int arrow_color)
{
    // Arrow length
    int arrow_length = 80;
    
    // Calculate the coordinates of the arrow tip
    int tip_x = center_x + (int)(cos_val * arrow_length);
    int tip_y = center_y - (int)(sin_val * arrow_length); // Invert y because screen y-axis is positive downward
    
    // Draw the arrow body (line from center to tip)
    draw_line(center_x, center_y, tip_x, tip_y, arrow_color);
    
    // Note: This version only draws the main line of the arrow
    // To add arrowheads, additional code would be needed here
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
    for (x = 0; x < 320; x++)
        for (y = 0; y < 240; y++)
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
    volatile short int *one_pixel_address;
    // Calculate the memory address of the pixel
    // Pixel buffer uses (y << 10) + (x << 1) addressing scheme
    one_pixel_address = (volatile short int *)(pixel_buffer_start + (y << 10) + (x << 1));
    *one_pixel_address = line_color; // Set the pixel color
}
