#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "assign02.pio.h"
#include "hardware/watchdog.h"

#define IS_RGBW true        // To indicate we will use RGBQ format
#define NUM_PIXELS 1        // Number of WS2812 devices in the chain
#define WS2812_PIN 28       // The GPIO pin the WS2812 is connected to
#define MAX_CHARS 36        // Max number of alphanumeric characters available 
#define MAX_MORSE_INPUT 5   // Max number of morse code characters for a given character
#define MAX_LEVEL 2         // The last level that can be played 
#define MAX_LIVES 3         // Max number of lives player can have

// Global Variables
char required_answer[MAX_MORSE_INPUT], player_input[MAX_MORSE_INPUT];
int current_level, lives, current_streak, charsEntered, correct_ans, incorrect_ans, num_tries;
bool input_start, game_is_complete; // input_start indicates start of player input per round, game_is_complete indicates game over/completion

// Arrays storing the characters and their equivalent in Morse code
const char alphaNumChars[MAX_CHARS] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};
const char *morseCode[MAX_CHARS] = {"-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.", ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.."};

// Declaration of the main assembly
void main_asm();

/**
 * @brief   Calls the gpio_init() function from the SDK to initialise 
 *          a pin
 * 
 * @param pin   The GPIO pin number to initialise
 */
void asm_gpio_init(uint pin) {
    gpio_init(pin);
}


/**
 * @brief   Calls the gpio_set_dir() function from the SDK to 
 *          set the direction of a GPIO pin
 * 
 * @param pin   The GPIO pin number to initialise
 * @param out   Set the direction of the input pin 
 */
void asm_gpio_set_dir(uint pin, bool out) {
    gpio_set_dir(pin, out);
}

/**
 * @brief   Calls the gpio_set_irq_enabled() SDK funtion to initialise the
 *          falling edge interrupt for a chosen GPIO pin
 * 
 * @param pin   The GPIO pin number to initialise
 */
void asm_gpio_set_irq_fall(uint pin) {
    gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_FALL, true);
}


/**
 * @brief   Calls the gpio_set_irq_enabled() SDK funtion to initialise the
 *          rising edge interrupt for a chosen GPIO pin
 * 
 * @param pin   The GPIO pin number to initialise
 */
void asm_gpio_set_irq_rise(uint pin) {
    gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE, true);
}


/**
 * @brief Calls the function that pushes a 32-bit RGB colour
 *        onto the LED on the board
 * 
 * @param pixel_grb The 32-bit colour value generated by urgb_u32()
 */
void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}


/**
 * @brief Calls the watchdog_enable?() function from the SDK to 
 *        enable the watchdog timer and set it to trigger a 
 *        reset after a chosen time interval
 * 
 */
void init_watchdog_timer() {
    watchdog_enable(9000000, 1); // Enable watchdog timer and set to trigger reset every 9s
}


/**
 * @brief Calls the watchdog_update() function from the SDK 
 *        which resets the watchdog counter using the time
 *        specified in watchdog_enable so that it does not
 *        reset the application
 * 
 */
void reset_watchdog_timer() {
    watchdog_update(); 
}


/**
 * @brief Function that generates a 32-bit composit GRB
 *        value by using the 8-bit paramaters for
 *        red, green and blue.
 * 
 * @param red       The 8-bit intensity value for the red component
 * @param green     The 8-bit intensity value for the green component
 * @param blue      The 8-bit intensity value for the blue component
 * @return uint32_t Returns the resulting composit 32-bit RGB value
 */
static inline uint32_t urgb_u32(uint8_t red, uint8_t green, uint8_t blue) {
    return  ((uint32_t) (red) << 8)  |
            ((uint32_t) (green) << 16) |
            (uint32_t) (blue);
}


/**
 * @brief Set the LED to blue to indicate game is not in progress
 * 
 */
void set_LED_blue() {
    put_pixel(urgb_u32(0x00, 0x00, 0x1F));
}

/**
 * @brief Set the LED to green to indicate player has 3 lives left
 * 
 */
void set_LED_green() {
    put_pixel(urgb_u32(0x00, 0x1F, 0x00));
}

/**
 * @brief Set the LED to yellow to indicate player has 2 lives left 
 * 
 */
void set_LED_yellow() {
    put_pixel(urgb_u32(0x1F, 0x1F, 0x00));
}

/**
 * @brief Set the LED to orange to indicate player has 1 life left
 * 
 */
void set_LED_orange() {
    put_pixel(urgb_u32(0x1F, 0xF, 0x00));
}

/**
 * @brief Set the LED to red to indicate player has no lives left
 * 
 */
void set_LED_red() {
    put_pixel(urgb_u32(0x1F, 0x00, 0x00));
}


/**
 * @brief Changes the colour of the LED depending on the number of lives left
 * 
 */
void change_LED_colour() {
    if (lives == 3){
        set_LED_green();
    }
    else if (lives == 2){
        set_LED_yellow();
    }
    else if (lives == 1){
        set_LED_orange();
    }
    else if (lives == 0){
        set_LED_red();
    }
}

/**
 * @brief 
 *        Randomly chooses a character for the player to try to write in Morse Code
 *      
 */
void choose_char() { 
    int randomChar = 0; 

    if(current_level == 1) {
        // Level 1 - character and its equivalent in Morse are both printed
        randomChar = rand() % 36;
        printf("The character you are to enter is: %c or '%s' in Morse Code.\n", alphaNumChars[randomChar], morseCode[randomChar]);           
    }
    if(current_level == 2) {
        // Level 2 - only the character is printed
        randomChar = rand() % 36;
        printf("The character you are to enter is: %c\n", alphaNumChars[randomChar]);
    }

    strcpy(required_answer, morseCode[randomChar]); // Store chosen character into required_answer

    // Reset values
    input_start = true; 
    charsEntered = 0; // Counts number of Morse characters player has put in so far
    player_input[0] = '\0'; // Empty player input array
    num_tries++;

    printf("Answer entered so far: ");
}


/** 
* @brief Prints instructions and resets values once application is started
* 
*/
void opening_sequence() { 
    set_LED_blue();

    // Print Instructions:
    printf("\n\n-----------------------------------------------------------------------------------------------------------------------\n");
    printf("Morse Code Game - Group 11\n\n");
    printf("How to Play:\n");

    printf("Interact with the game by pressing the GP21 button on the MAKER-PI-PICO board\n\n\n");
    printf("-Press for a duration between 0-1 seconds to input a Morse \"dot\" \n\n");
    printf("-Press for a duration greater than 1 seconds to input a Morse \"dash\" \n\n");
    printf("-If nothing is input for at least 1 second in between button presses a \"space\" character will be input\n\n");
    printf("-If no new input is entered for at least 2 seconds after button is released, the sequence will be considered complete\n\n");
    printf("-Your answer will then be checked for correctness\n\n\n");

    printf("\nThe application will automatically reset after idling for 9 seconds\n\n");

    printf("\n-----------------------------------------------------------------------------------------------------------------------\n");

    // prompt player to choose difficulty level 
    printf("Choose difficulty level - enter \".----\" (1) or  \"..---\" (2) \n");
    printf("Entered: ");

    // Reset values
    player_input[0] = '\0'; // Empty player input array
    input_start = true;
    game_is_complete = false;
    lives = 3;
    current_streak = 0;
    charsEntered = 0;
    correct_ans = 0;
    incorrect_ans = 0;
    num_tries = 0;
}

/** 
* @brief Checks what level the player has chosen from opening screen
*  
* @return int      Returns 1 or 0 depending on if player is finished choosing
*/
int choose_level() {
    if (charsEntered == MAX_MORSE_INPUT) { // Check answer once player has entered 5 characters
        if (!strcmp(player_input, morseCode[1])) {
            // Player entered .---- (1)
            printf("\n\nLevel 1 selected. Characters will be shown with their morse code equivalent.\n\n");
            current_level = 1; 
            set_LED_green();
            choose_char();
            return 1; // Return 1 to signify player finished choosing level
        }
        else if (!strcmp(player_input, morseCode[2])) {
            // Player entered ..--- (2) 
            printf("\n\nLevel 2 selected. Only characters will be shown.\n\n");
            current_level = 2;
            set_LED_green(); 
            choose_char();
            return 1; // Return 1 to signify player finished choosing level
        }
        else { 
            // Player did not enter either 1 or 2 in Morse Code
            printf(" Invalid entry. Try again.\n");
            printf("Entered: ");

            // Reset values
            input_start = true;
            charsEntered = 0;
            player_input[0] = '\0';
            return 0; // Return 0 to signify player is still choosing a level
        }
    }
    return 0; // Player is still entering characters, return 0
}

 
/**
 * @brief   Adds the player's newest input into player_input 
 *          depending on number passed into function
 * 
 * @param new_input  int number indicating player's newest input
 */
void add_input(int new_input) { 
    // 1 = "-" , 2 = "." , 3 = " "
    if (charsEntered < MAX_MORSE_INPUT) { 
        // Only add characters to player_input array if max number of characters not met
        if (new_input == 1) {
            // Player entered a dash
            strcat(player_input, "-"); 
            printf("-");
            charsEntered++;
            if (input_start == true) {
                input_start = false;
            }
        }
        else if (new_input == 2) {
            // Player entered a dot
            strcat(player_input, "."); 
            printf(".");
            charsEntered++;
            if (input_start == true) {
                input_start = false;
            }
        }
        else if (input_start == false) {
            // Player entered a space and it is not the first input 
            strcat(player_input, " "); 
            printf(" ");
            charsEntered++;
        }
    }
}


/**
 * @brief Print the player's statistics for each level
 * 
 */
void print_statistics() {
    printf("\n--------------------------------------------------------------------------------\n");
    printf("Your performance this level:\n\n");
    printf("Total number of tries: %d\n", num_tries);
    printf("Total correct answers: %d\n", correct_ans);
    printf("Total incorrect answers: %d\n", incorrect_ans);
    printf("--------------------------------------------------------------------------------\n\n");
}

/**
 * @brief   Funtion to check whether the the game is complete or not 
 * 
 * @return int  Returns 1 to indicate game completion and 0 to indicate that the game is not complete
 */
int check_if_game_complete() {
    if (game_is_complete == true) {
        return 1; // Game is complete, return 1
    }
    else {
        return 0; // Game is not complete, return 0
    }
}


/**
 * @brief Function to handle when a player has either lost or 
 *        completed the game
 * 
 */
void game_complete() { 
    game_is_complete = true;
    if (lives == 0) { // Player has lost game, print game over
        printf("\n--------------------------------------------------------------------------------\n");
        printf("GAME OVER!\n");
        printf("If you would like to replay, wait 9 seconds for the application to restart.\n");
        printf("--------------------------------------------------------------------------------\n");
    }
    else { // Player has won game, print completion message
        set_LED_blue();
        printf("\n--------------------------------------------------------------------------------");
        printf("\n\nCongratulations! You have completed the game.\n\n");
        printf("If you would like to replay, wait 9 seconds for the application to restart.\n");
        printf("--------------------------------------------------------------------------------\n");
    }
}


/**
 * @brief Function to move on to the next level 
 *        on the basis of the player's current level
 *        If the player's current level is greater than MAX_LEVEL then the game completes
 * 
 */
void next_level() { 
    // Reset values
    current_level++;
    current_streak = 0;
    charsEntered = 0;
    num_tries = 0;
    correct_ans = 0;
    incorrect_ans = 0;

    if (current_level > MAX_LEVEL) { // Last level has been completed
        game_complete();
    }
    else { // print message indicating to the player that they have reached the next level
        printf("\n--------------------------------------------------------------------------------");
        printf("\nYou have now reached level %d!\n", current_level);
        printf("--------------------------------------------------------------------------------\n\n");
        choose_char(); // Choose a new character
    }
}

/**
 * @brief Function to check whether the answer that the player has input 
 *        matches the required answer
 * 
 */
void check_answer() {
    bool found = false;
    int found_letter;

    // Finds out if the player's morse code input matches any of the morse code stored in the morseCode array
    for(int i = 0; i < MAX_CHARS; i++){
        if(!strcmp(player_input, morseCode[i])){
            found_letter = i; // Input matches one of the morse sequences in array, store the index of this match
            found = true;
            break;
        }
    }
   
   // Prints whether the player's input has an equivalent character or not
    if(found) { 
        printf("\nYou typed \'%s\' - this is char: %c\n", player_input, alphaNumChars[found_letter]);
    } 
    else { // Input does not match any of the stored morse sequences
        printf("\nYou typed \'%s\' - this is char: ?\n", player_input);
    }

    // Checks if player's input matches the required answer
    if(!strcmp(player_input, required_answer)){ // Player's answer is correct
        printf("Congratulations - this is the correct answer.\n\n");
        current_streak++;
        correct_ans++;
        if (lives < MAX_LIVES) {
            lives++;
        }
        change_LED_colour();
    } 
    else { // Player's answer is incorrect
        printf("Sorry - this is the incorrect answer.\n\n");
        lives--;
        incorrect_ans++;
        current_streak = 0;
        change_LED_colour();
    }
    printf("Current lives: %d. Current streak: %d\n\n\n", lives, current_streak);

    if (lives == 0) { // If player loses all their lives print statistics and game over message
        print_statistics();
        game_complete();
    }
    else {
        if (current_streak == 5) { // Check if player has answered correctly 5 times in a row
            print_statistics();
            next_level(); // Proceed to the next level
        }
        else { // Continue with current level, pick a new character for player
            choose_char();
        }
    }
} 


/**
 * @brief Function to calculate the time difference between two time values
 * 
 * @param current_time      The current time value
 * @param previous_time     The previous time value
 * @return int              Return the time difference
 */
int calculate_duration(int current_time, int previous_time) {
    return (current_time - previous_time);
}


/**
 * @brief       Uses the assembly code to run the game in which
 *              the player plays by pressing the GP21 button
 * 
 * @return int  Returns exit-status zero on completion
 */
int main() {
    stdio_init_all();              // Initialise all basic IO
    srand(time(NULL));             // Seed for random number generator

    // Initialise the PIO interface with the WS2812 code
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, 0, offset, WS2812_PIN, 800000, IS_RGBW);

    main_asm();                    // Call the main assembly code
    return(0);
}