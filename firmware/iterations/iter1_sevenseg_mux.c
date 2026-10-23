#include <mega32.h>
#include <delay.h>
#include <stdio.h>
#include <string.h>
#include <alcd.h>

// ---CONFIGURATION---
#define ADMIN_PASSWORD "1357"
#define TOTAL_VOTERS 5
#define VOTE_TIME_LIMIT_MINUTES 2
#define ID_LENGTH 5 // 4 digits + 1 for the end-of-string character '\0'

// ---HARDWARE DEFINITIONS---
// This section defines simple names for hardware ports and pins.
#define KEYPAD_PORT PORTB
#define KEYPAD_DDR  DDRB
#define KEYPAD_PIN  PINB

#define VOTE_BUTTON_PIN PINA
#define VOTE_BUTTON_DDR DDRA
#define CANDIDATE_1_BUTTON 0
#define CANDIDATE_2_BUTTON 1
#define CANDIDATE_3_BUTTON 2

#define SEGMENT_PORT PORTD
#define DIGIT_PORT   PORTC
#define DIGIT_DDR    DDRC

// ---GLOBAL VARIABLES---
// These variables are accessible by all functions.

// Stores the IDs of all eligible voters
char voter_id_database[TOTAL_VOTERS][ID_LENGTH] = {
    "1111", "2222", "3333", "4444", "5555"
};
// Tracks who has already voted
unsigned char has_voted_status[TOTAL_VOTERS] = {0, 0, 0, 0, 0};

// Stores the vote count for each of the 3 candidates
unsigned int candidate_votes[3] = {0, 0, 0};

// Text buffers for displaying information on the LCD
char lcd_display_text[17];

// Timer variables used by the interrupt
unsigned char seconds = 0;
unsigned char minutes = 0;

// Voting state flags
bit is_voting_active = 0;
bit has_voting_ended = 0;

// Variables to store the final result
int winner_candidate_index = -1;
bit is_a_tie = 0;

// ---FUNCTION PROTOTYPES---
// A list of all the functions we will use.
void initialize_system(void);
void handle_admin_login(void);
void wait_for_start_signal(void);
void run_voting_session(void);
void determine_winner(void);
void display_final_results_loop(void);

char get_keypad_press(void);
void read_keypad_string(char input_array[], int len);
int check_voter_id(char id_to_check[]);
void trigger_end_of_voting(void);

// ---TIMER INTERRUPT---
// This special function runs automatically in the background to keep time.
interrupt [TIM1_OVF] void timer1_ovf_isr(void) {
    // CORRECTED: All declarations must be at the very top of the block.
    static unsigned int millisecond_counter = 0;
    static unsigned char active_display_digit = 0;
    unsigned char seven_segment_patterns[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

    // Re-initialize timer for the next interrupt
    TCNT1H = 0xF0;
    TCNT1L = 0x00;
    
    // Timer clock logic
    if (is_voting_active) {
        // This block runs approximately every 1 second
        millisecond_counter++;
        if (millisecond_counter >= 250) {
            millisecond_counter = 0;
            seconds++;
            if (seconds >= 60) {
                seconds = 0;
                minutes++;
            }
        }
    }
    
    // 7-Segment display logic
    PORTC &= 0x0F; // Turn off all digits
    active_display_digit = (active_display_digit + 1) % 4;

    if (active_display_digit == 0)      { PORTD = seven_segment_patterns[minutes / 10]; PORTC.4 = 1; }
    else if (active_display_digit == 1) { PORTD = seven_segment_patterns[minutes % 10]; PORTC.5 = 1; }
    else if (active_display_digit == 2) { PORTD = seven_segment_patterns[seconds / 10]; PORTC.6 = 1; }
    else if (active_display_digit == 3) { PORTD = seven_segment_patterns[seconds % 10]; PORTC.7 = 1; }
}

//******************************************************************************
// --- MAIN PROGRAM ---
// This is where the program starts. It calls each function step-by-step.
//******************************************************************************
void main(void) {
    // Step 1: Set up all the hardware and timers.
    initialize_system();

    // Step 2: Wait for the admin to log in with the correct password.
    handle_admin_login();
    
    // Step 3: Wait for the admin to press the '*' key to begin.
    wait_for_start_signal();
    
    // Step 4: Run the main voting loop where voters cast their ballots.
    run_voting_session();
    
    // Step 5: After voting ends, calculate who the winner is.
    determine_winner();
    
    // Step 6: Display the final scores and the winner indefinitely.
    display_final_results_loop();
}

//******************************************************************************
// --- SETUP AND LOGIN FUNCTIONS ---
//******************************************************************************

// This function handles all initial hardware and timer setup.
void initialize_system(void) {
    // Setup Ports (DDR = Data Direction Register, PORT = initial value)
    VOTE_BUTTON_DDR = 0xF0; // PA0-2 inputs for buttons, PA4-7 outputs for LCD
    PORTA = 0x07;           // Enable pull-ups on PA0-2
    KEYPAD_DDR = 0x0F;      // Keypad Rows out, Columns in
    KEYPAD_PORT = 0xF0;     // Enable pull-ups on columns
    DIGIT_DDR = 0xFF;       // LCD Control & 7-Seg Digits
    DDRD = 0xFF;            // 7-Seg Segments

    // Setup Timer1 to create an interrupt
    TCCR1A = 0x00;
    TCCR1B = 0x05; // Prescaler = 1024
    TCNT1H = 0xF0;
    TCNT1L = 0x00;
    TIMSK = (1<<TOIE1); // Enable Timer1 Overflow Interrupt

    // Initialize the LCD display
    lcd_init(16);
    lcd_clear();
    lcd_puts("E-Voting System");
    delay_ms(2000);
}

// This function loops until the correct admin password is entered.
void handle_admin_login(void) {
    char keypad_input[ID_LENGTH];
    while (1) {
        lcd_clear();
        lcd_puts("Admin Password:");
        lcd_gotoxy(0, 1);
        read_keypad_string(keypad_input, 4);
        if (strcmp(keypad_input, ADMIN_PASSWORD) == 0) {
            lcd_clear();
            lcd_puts("Access Granted!");
            delay_ms(1500);
            break; // Exit the loop on success
        } else {
            lcd_clear();
            lcd_puts("Wrong Password!");
            delay_ms(1500);
        }
    }
}

// This function waits for the admin to press the '*' key.
void wait_for_start_signal(void) {
    lcd_clear();
    lcd_puts("Press '*' to");
    lcd_gotoxy(0,1);
    lcd_puts("Start Voting");
    while (get_keypad_press() != '*');
}

//******************************************************************************
// --- CORE VOTING FUNCTIONS ---
//******************************************************************************

// This function contains the main loop for the entire voting session.
void run_voting_session(void) {
    char keypad_input[ID_LENGTH];
    int voter_db_position;
    unsigned char total_votes_cast = 0;

    // Start the clock and the voting process
    lcd_clear();
    lcd_puts("Voting Started!");
    delay_ms(1000);
    is_voting_active = 1;
    #asm("sei") // Enable global interrupts

    while (is_voting_active) {
        // Check if voting should end
        if (total_votes_cast >= TOTAL_VOTERS || minutes >= VOTE_TIME_LIMIT_MINUTES) {
            trigger_end_of_voting();
            break; // Exit the voting loop
        }

        // Ask for voter's ID
        lcd_clear();
        lcd_puts("Enter Voter ID:");
        lcd_gotoxy(0, 1);
        read_keypad_string(keypad_input, 4);
        voter_db_position = check_voter_id(keypad_input);

        // If the ID is valid and hasn't voted yet...
        if (voter_db_position != -1) { 
            lcd_clear();
            lcd_puts("Use Buttons to Vote");
            
            // Wait for a vote button to be pressed
            while(1) {
                if (VOTE_BUTTON_PIN.CANDIDATE_1_BUTTON == 0) {
                    delay_ms(50);
                    if (VOTE_BUTTON_PIN.CANDIDATE_1_BUTTON == 0) {
                         candidate_votes[0]++;
                         while(VOTE_BUTTON_PIN.CANDIDATE_1_BUTTON == 0); // Wait for button release
                         break;
                    }
                }
                if (VOTE_BUTTON_PIN.CANDIDATE_2_BUTTON == 0) {
                    delay_ms(50);
                    if (VOTE_BUTTON_PIN.CANDIDATE_2_BUTTON == 0) {
                        candidate_votes[1]++;
                        while(VOTE_BUTTON_PIN.CANDIDATE_2_BUTTON == 0);
                        break;
                    }
                }
                if (VOTE_BUTTON_PIN.CANDIDATE_3_BUTTON == 0) {
                    delay_ms(50);
                     if (VOTE_BUTTON_PIN.CANDIDATE_3_BUTTON == 0) {
                        candidate_votes[2]++;
                        while(VOTE_BUTTON_PIN.CANDIDATE_3_BUTTON == 0);
                        break;
                    }
                }
            }
            
            // Mark voter as "voted" and thank them
            has_voted_status[voter_db_position] = 1;
            total_votes_cast++;
            lcd_clear();
            lcd_puts("Thank You!");
            delay_ms(2000);
        } else {
            // If ID was invalid or already used
            delay_ms(2000);
        }
    }
}

// This function stops the clock and sets the "ended" flag.
void trigger_end_of_voting(void) {
    #asm("cli") // Disable global interrupts
    is_voting_active = 0;
    has_voting_ended = 1;
}

//******************************************************************************
// --- FINAL RESULT FUNCTIONS ---
//******************************************************************************

// This function analyzes the votes to find the winner or a tie.
void determine_winner(void) {
    if (has_voting_ended) {
        int i;
        int highest_vote_count = 0;
        int candidates_with_max_votes = 0;
        
        // First, find what the highest score was
        for (i = 0; i < 3; i++) {
            if (candidate_votes[i] > highest_vote_count) {
                highest_vote_count = candidate_votes[i];
            }
        }
        
        // Next, check how many candidates got that score
        for (i = 0; i < 3; i++) {
            if (candidate_votes[i] == highest_vote_count) {
                candidates_with_max_votes++;
                winner_candidate_index = i;
            }
        }
        
        // It's a tie if more than one candidate has the top score, or if nobody voted
        if (candidates_with_max_votes > 1 || highest_vote_count == 0) {
            is_a_tie = 1;
        }
    }
}

// This function shows the results on the LCD in an infinite loop.
void display_final_results_loop(void) {
    while (1) {
       if (has_voting_ended) {
          // Screen 1: Show the final scores
          lcd_clear();
          lcd_puts("Voting Has Ended");
          lcd_gotoxy(0, 1);
          sprintf(lcd_display_text, "C1:%d C2:%d C3:%d", candidate_votes[0], candidate_votes[1], candidate_votes[2]);
          lcd_puts(lcd_display_text);
          delay_ms(4000);

          // Screen 2: Show the winner or a tie
          lcd_clear();
          if (is_a_tie) {
              lcd_puts("Result: IT'S A TIE");
          } else {
              sprintf(lcd_display_text, "Winner: Candidate %d", winner_candidate_index + 1);
              lcd_puts(lcd_display_text);
          }
          delay_ms(4000);
       }
    }
}

//******************************************************************************
// --- HELPER FUNCTIONS ---
//******************************************************************************

// Reads a single key press from the 3x4 keypad.
char get_keypad_press(void) {
    unsigned char row, col;
    const char keymap[4][3] = {
        {'1', '2', '3'}, {'4', '5', '6'}, {'7', '8', '9'}, {'*', '0', '#'}
    };
    while (1) {
        for (row = 0; row < 4; row++) {
            KEYPAD_PORT = ~(1 << row); 
            delay_us(10); 
            if (KEYPAD_PIN.4 == 0) { col = 0; }
            else if (KEYPAD_PIN.5 == 0) { col = 1; }
            else if (KEYPAD_PIN.6 == 0) { col = 2; }
            else { continue; }
            while (KEYPAD_PIN.4 == 0 || KEYPAD_PIN.5 == 0 || KEYPAD_PIN.6 == 0);
            return keymap[row][col];
        }
    }
}

// Reads multiple key presses to form a string (like a password or ID).
void read_keypad_string(char input_array[], int len) {
    int i;
    for (i = 0; i < len; i++) {
        input_array[i] = get_keypad_press();
        lcd_putchar(input_array[i]);
    }
    input_array[i] = '\0'; // Add the end-of-string character
}

// Checks if an ID is in the database and if that person has already voted.
int check_voter_id(char id_to_check[]) {
    int i;
    for (i = 0; i < TOTAL_VOTERS; i++) {
        if (strcmp(id_to_check, voter_id_database[i]) == 0) {
            if (has_voted_status[i] == 1) {
                lcd_clear();
                lcd_puts("Already Voted");
                return -1; // -1 means error
            }
            return i; // Return the voter's position in the database
        }
    }
    lcd_clear();
    lcd_puts("Invalid Voter ID");
    return -1; // -1 means error
}