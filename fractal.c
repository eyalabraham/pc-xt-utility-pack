/**************************************************
 *   fractal.c
 *
 *      Draw a Mandelbrot fractal using INT10 graphics BIOS calls
 *
 *      resources:
 *          https://en.wikipedia.org/wiki/Mandelbrot_set
 */

#include    <stdio.h>
#include    <stdint.h>
#include    <math.h>
#include    <dos.h>
#include    <i86.h>

#define     X_RES       319.0
#define     Y_RES       199.0
#define     MAX_ITER    100
#define     FRAC_X_MIN -2.5
#define     FRAC_X_MAX  1.0
#define     FRAC_Y_MIN -1.0
#define     FRAC_X_MAX  1.0

typedef enum Modes
    {
        res80x25colortx = 3,        // 80x25 16 color text (CGA,EGA,MCGA,VGA)
        res320x200color4 = 4,       // 320x200 4 color graphics (CGA,EGA,MCGA,VGA)
        res640x200bw = 6,           // 640x200 B/W graphics (CGA,EGA,MCGA,VGA)
        res80x25monotx = 7,         // 80x25 Monochrome text (MDA,HERC,EGA,VGA)
        res320x200color16 = 0x0D,   // 320x200 16 color graphics (EGA,VGA)
        res640x200color16 = 0x0E,   // 640x200 16 color graphics (EGA,VGA)
        res640x350bw = 0x0F,        // 640x350 Monochrome graphics (EGA,VGA)
        res640x350color16 = 0x10    // 640x350 16 color graphics (EGA or VGA with 128K)
    } vid_mode_t;

/**************************************************
 *  function prototypes
 */
void vid_set_mode(vid_mode_t);
void vid_set_palette(int);
void vid_put_pixel(int, int, uint8_t);

/**************************************************
 *  globals
 */
union  REGS     regs;
struct SREGS    segment_regs;
int             iterations[4] = {MAX_ITER, 15, 5, 0};   // keep MAX_ITER and '0', change all other numbers

/* in addition to compiler switch -fpc these global definitions are
 * required to avoid including 8087 code and libraries
 * source: https://sourceforge.net/p/openwatcom/discussion/general/thread/6522a1c0/?limit=25
 */
unsigned char   _WCNEAR _8087 = 0;
unsigned char   _WCNEAR _real87 = 0;

/**************************************************
 *  main()
 *
 *   Exit with 0 on success, or negative exit code if error
 */
int main(int argc, char *argv[])
{
    float   scale_x, scale_y;
    float   temp, x, y, x0, y0;
    int     iteration, hx, hy, color;
    int     max_iter = -1, min_iter = 9999;

    printf("fractal %s %s\n", __DATE__, __TIME__);

    /* select video mode and palette
     */
    vid_set_mode(res320x200color4);
    //vid_set_mode(res640x350color16);
    vid_set_palette(0);

    /* draw fractal
     */
    scale_x = (fabs(FRAC_X_MIN) + fabs(FRAC_X_MAX)) / (X_RES+1.0);
    scale_y = (fabs(FRAC_Y_MIN) + fabs(FRAC_X_MAX)) / (Y_RES+1.0);

    for (hy = 1; hy <= Y_RES; hy++)
    {
        for (hx=1; hx <= X_RES; hx++)
        {
            x0 = scale_x * hx + FRAC_X_MIN;
            y0 = FRAC_X_MAX - scale_y * hy;
            x = 0.0; y = 0.0;

            for ( iteration = 0; iteration < MAX_ITER; iteration++ )
            {
                temp = x*x-y*y+x0;
                y = 2.0*x*y+y0;
                x = temp;
                if ( (x*x+y*y) > 4.0)
                    break;
            }

            for ( color = 0; color < 4; color++ )
            {
                if ( iteration < iterations[color] )
                    continue;
                else
                    {
                        vid_put_pixel(hx, hy, color);
                        break;
                    }

            }
        }
    }

    //while (1) {};
    sleep(15);
    vid_set_mode(res80x25colortx);

    return 0;
}

/**************************************************
 *  vid_set_mode()
 *
 *   Set video mode
 *
 *   param:  video mode number
 *   return: none
 */
void vid_set_mode(vid_mode_t mode)
{
    regs.h.ah = 0;
    regs.h.al = (uint8_t) mode;

    int86x(0x10, &regs, &regs, &segment_regs);
}

/**************************************************
 *  vid_set_palette()
 *
 *   Set video color palette
 *
 *   param:  color palette and color
 *   return: none
 */
void vid_set_palette(int palette_id)
{
    regs.h.ah = 0x0b;
    regs.h.bh = 1;
    regs.h.bl = palette_id ? 1 : 0;

    int86x(0x10, &regs, &regs, &segment_regs);
}

/**************************************************
 *  vid_put_pixel()
 *
 *   Draw a pixel
 *
 *   param:  pixel x and y coordinates and pixel color
 *   return: none
 */
void vid_put_pixel(int x, int y, uint8_t color)
{
    regs.h.ah = 0x0c;
    regs.h.al = color;
    regs.h.bh = 0;
    regs.w.cx = (uint16_t)x;
    regs.w.dx = (uint16_t)y;

    int86x(0x10, &regs, &regs, &segment_regs);
}
