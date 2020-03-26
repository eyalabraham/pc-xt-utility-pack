/*
 * sudoku.c
 *
 *  Based on Professor Thorsten Altenkirch's video on a recursive Sudoku solver
 *  found here (https://www.youtube.com/watch?v=G_UYXzGuqvM) in the Computerphile channel.
 *  This is a brute force recursive algorithm with a 'back tracking' method.
 *  The program relies on the functionality of ANSI.SYS for screen presentation.
 *
 *  TODO
 *   - The algorithm does not check if the puzzle is valid
 *   - If puzzle is not valid or there is no solution the solver never exits
 *   - Need to add a Ctrl-C / Ctrl-Break to abort the program
 *
 */
#define     __STDC_WANT_LIB_EXT1__  1               // safe library function calls

#include    <stdlib.h>
#include    <stdio.h>
#include    <conio.h>

/* -----------------------------------------
   Definitions
----------------------------------------- */
#define     ESC                         27
#define     VT100_CLEAR_SCREEN          printf("%c[2J", ESC);
#define     VT100_CLEAR_LINE            printf("%c[2K", ESC);
#define     VT100_POSITION_CURSOR(x,y)  printf("%c[%-d;%-dH", ESC, y, x);
#define     VT100_BOLD_TEXT             printf("%c[1m", ESC);
#define     VT100_NORMAL_TEXT           printf("%c[0m", ESC);

/* -----------------------------------------
   Types and data structures
----------------------------------------- */

/* -----------------------------------------
   Globals
----------------------------------------- */

// Line grid
char    top_row[]    = {201, 205, 209, 205, 209, 205, 203, 205, 209, 205, 209, 205, 203, 205, 209, 205, 209, 205, 187, 0};
char    box_row1[]   = {186,  32, 179,  32, 179,  32, 186,  32, 179,  32, 179,  32, 186,  32, 179,  32, 179,  32, 186, 0};
char    box_row2[]   = {199, 196, 197, 196, 197, 196, 215, 196, 197, 196, 197, 196, 215, 196, 197, 196, 197, 196, 182, 0};
char    box_row3[]   = {204, 205, 216, 205, 216, 205, 206, 205, 216, 205, 216, 205, 206, 205, 216, 205, 216, 205, 185, 0};
char    bottom_row[] = {200, 205, 207, 205, 207, 205, 202, 205, 207, 205, 207, 205, 202, 205, 207, 205, 207, 205, 188, 0};

// Number grid pre-filled with sample
int     grid[9][9] = {{5, 3, 0, 0, 7, 0, 0, 0, 0},
                      {6, 0, 0, 1, 9, 5, 0, 0, 0},
                      {0, 9, 8, 0, 0, 0, 0, 6, 0},
                      {8, 0, 0, 0, 6, 0, 0, 0, 3},
                      {4, 0, 0, 8, 0, 3, 0, 0, 1},
                      {7, 0, 0, 0, 2, 0, 0, 0, 6},
                      {0, 6, 0, 0, 0, 0, 2, 8, 0},
                      {0, 0, 0, 4, 1, 9, 0, 0, 5},
                      //{0, 0, 0, 0, 8, 0, 0, 0, 0}
                      {0, 0, 0, 0, 8, 0, 0, 7, 9}
                     };

/*
int     grid[9][9] = {{0, 7, 0, 2, 5, 0, 4, 0, 0},
                      {8, 0, 0, 0, 0, 0, 9, 0, 3},
                      {0, 0, 0, 0, 0, 3, 0, 7, 0},
                      {7, 0, 0, 0, 0, 4, 0, 2, 0},
                      {1, 0, 0, 0, 0, 0, 0, 0, 7},
                      {0, 4, 0, 5, 0, 0, 0, 0, 8},
                      {0, 9, 0, 6, 0, 0, 0, 0, 0},
                      {4, 0, 1, 0, 0, 0, 0, 0, 5},
                      {0, 0, 7, 0, 8, 2, 0, 3, 0}
                     };
*/

/* -----------------------------------------
   Function prototypes
----------------------------------------- */
void    draw_grid(void);
void    fill_grid(void);
void    get_number_grid(void);
int     is_possible_number(int, int, int);
void    solve(void);

/*------------------------------------------------
 * main()
 *
 *
 */
int main(int argc, char* argv[])
{
    int     ok = 0;
    int     key;

    while ( !ok )
    {
        get_number_grid();

        VT100_CLEAR_SCREEN;

        draw_grid();

        fill_grid();

        VT100_POSITION_CURSOR(1, 21);
        VT100_CLEAR_LINE

        printf("Proceed to solve [Y/n]?\n");
        key = getch();
        if ( key == 'Y' || key == 'y' )
            ok = 1;
    }

    VT100_POSITION_CURSOR(1, 21);
    VT100_CLEAR_LINE
    printf("Solving...");

    solve();

    VT100_CLEAR_SCREEN;

    return 0;
}

/*------------------------------------------------
 * draw_grid()
 *
 *  Draw line grid for the Sudoku number grid
 *
 *  param:  Nothing
 *  return: Nothing
 */
void draw_grid(void)
{
    int     i;

    VT100_POSITION_CURSOR(1,1);

    printf("%s\n", top_row);

    for ( i = 0; i < 2; i++ )
    {
        printf("%s\n", box_row1);
        printf("%s\n", box_row2);
        printf("%s\n", box_row1);
        printf("%s\n", box_row2);
        printf("%s\n", box_row1);
        printf("%s\n", box_row3);
    }

    printf("%s\n", box_row1);
    printf("%s\n", box_row2);
    printf("%s\n", box_row1);
    printf("%s\n", box_row2);
    printf("%s\n", box_row1);
    printf("%s\n", bottom_row);
}

/*------------------------------------------------
 * fill_grid()
 *
 *  Simple screen printing of the 9x9 Sudoku number grid.
 *
 *  param:  Global grid 9x9 integer array.
 *  return: Nothing
 */
void fill_grid(void)
{
    int     r, c;

    VT100_BOLD_TEXT;

    for ( r = 0; r < 9; r++ )
    {
        for ( c = 0; c < 9; c++ )
        {
            if ( grid[r][c] != 0 )
            {
                VT100_POSITION_CURSOR(2 * (c + 1), 2 * (r + 1));
                putchar(('0' + grid[r][c]));
            }
        }
    }

    VT100_NORMAL_TEXT;
}

/*------------------------------------------------
 * get_number_grid()
 *
 *  Get input numbers for the Sudoku grid.
 *
 *  param:  Nothing
 *  return: Nothing, updates the global 9x9 number grid
 */
void get_number_grid(void)
{
    int     r, c, ok;

    printf("Enter 9 numbers between 1 and 9 separated with spaces.\n");
    printf("Enter a 0 for a Sudoku grid location that is empty.\n");

    for ( r = 0; r < 9; r++ )
    {
        ok = 0;
        while ( !ok )
        {
            printf("Grid row %d: ", (r + 1));
            scanf("%d %d %d %d %d %d %d %d %d", &grid[r][0], &grid[r][1], &grid[r][2],
                                                &grid[r][3], &grid[r][4], &grid[r][5],
                                                &grid[r][6], &grid[r][7], &grid[r][8]);
            ok = 1;
            for ( c = 0; c < 9; c++ )
            {
                if ( grid[r][c] < 0 || grid[r][c] > 9 )
                    ok = 0;
            }
        }
    }
}

/*------------------------------------------------
 * is_possible_number()
 *
 *  Apply Sudoku rules to check of a number can be
 *  placed in position (row,col).
 *  The function references the global grid 9x9 integer array.
 *  No range checking of row and col inputs.
 *
 *  param:  0-based row and column of the suggested position for the number passed
 *  return: 0 (false) cannot be positioned, >0 (true) can be positioned
 */
int is_possible_number(int row, int col, int number)
{
    int     r, c, r0, c0;

    // Sanity check
    if ( grid[row][col] != 0 )
        abort();

    // Test number on same row
    for ( c = 0; c < 9; c++ )
        if ( grid[row][c] == number )
            return 0;

    // Test number on same column
    for ( r = 0; r < 9; r++ )
        if ( grid[r][col] == number )
            return 0;

    // Test number of its grid of 3x3
    r0 = (row / 3) * 3;
    c0 = (col / 3) * 3;

    for ( r = 0; r < 3; r++ )
        for ( c = 0; c < 3; c++ )
            if ( grid[r0+r][c0+c] == number )
                return 0;

    return 1;
}


/*------------------------------------------------
 * solve()
 *
 *  Recursive solver function.
 *  The function references the global grid 9x9 integer array.
 *
 *  param:  None
 *  return: None, updates the global grid with the solution.
 */
void solve(void)
{
    int     r, c, n;

    for ( r = 0; r < 9; r++ )
    {
        for ( c = 0; c < 9; c++ )
        {
            // Find an empty cell
            if ( grid[r][c] == 0 )
            {
                // Check possibility to place a number in the empty position
                for ( n = 1; n < 10; n++ )
                {
                    if ( is_possible_number(r, c, n) )
                    {
                        // Place the guess and recurse
                        grid[r][c] = n;
                        VT100_POSITION_CURSOR(2 * (c + 1), 2 * (r + 1));
                        putchar(('0' + grid[r][c]));
                        solve();
                        // The guess was bad, so back-track here
                        grid[r][c] = 0;
                        VT100_POSITION_CURSOR(2 * (c + 1), 2 * (r + 1));
                        putchar(' ');
                    }
                }
                // None of the numbers fit in this location
                return;
            }
        }
    }

    // In case there are additional solutions
    VT100_POSITION_CURSOR(1, 21);
    VT100_CLEAR_LINE
    printf("Hit any key ...\n");
    getch();
}
