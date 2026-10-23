#include <mega32.h>
#include <delay.h>
#include <stdio.h>
#include <string.h>
#include <alcd.h>

// ---CONFIGURATION---
#define adminPassword "1357"
#define totalVoters 5
#define voteTimeLimitMinutes 2
#define idLength 5 

// ---HARDWARE DEFINITIONS---
#define keypadPort PORTB
#define keypadDdr  DDRB
#define keypadPin  PINB

#define voteButtonPort PINA
#define voteButtonDdr DDRA
#define candidate1Button 0
#define candidate2Button 1
#define candidate3Button 2

// CORRECTED: Port assignments for 7-segment display to avoid LCD conflict.
// Segments are now on PORTD, and Digit selection is on PORTC.
#define segmentPort PORTD
#define digitPort   PORTC

// ---GLOBAL VARIABLES---
char voterIdDatabase[totalVoters][idLength] = {
    "1111", "2222", "3333", "4444", "5555"
};
unsigned char hasVotedStatus[totalVoters] = {0, 0, 0, 0, 0};
unsigned int candidateVotes[3] = {0, 0, 0};
unsigned char seconds = 0;
unsigned char minutes = 0;
unsigned char isVotingActive = 0;

// ---HELPER FUNCTION PROTOTYPES---
char waitForKeypadPress(void);
void readStringFromKeypad(char inputArray[], int len);
void updateSevenSegmentDisplay(void);


// ---TIMER INTERRUPT---
interrupt [TIM1_COMPA] void timer1_compa_isr(void)
{
    if (isVotingActive == 1) {
        seconds++;
        if (seconds == 60) {
            seconds = 0;
            minutes++;
            if (minutes == 60) {
                minutes = 0;
            }
        }
    }
}


//******************************************************************************
// --- MAIN PROGRAM ---
//******************************************************************************
void main(void)
{
    // --- LOCAL VARIABLES ---
    char keypadInput[idLength];
    char lcdDisplayText[17];
    int voterDbPosition;
    unsigned char totalVotesCast = 0;
    int i, j, k; // General-purpose loop counters
    int voterFound;
    int highestVoteCount;
    int winnerCandidateIndex;
    unsigned char isATie;
    int candidatesWithMaxVotes;
    unsigned char currentScreen;

    // --- STEP 1: INITIALIZE THE SYSTEM ---
    voteButtonDdr = 0xF0; 
    PORTA = 0x07;           
    keypadDdr = 0x0F;      
    keypadPort = 0xF0;     
    
    // CORRECTED: Setup for PORTC and PORTD
    DDRD = 0xFF; // PORTD is now fully output for the 7-segment patterns.
    DDRC = 0xFF; // PORTC is output for LCD control (lower pins) and 7-seg digits (upper pins).
    
    TCCR1A = 0x00;
    TCCR1B = (1<<WGM12) | (1<<CS12) | (1<<CS10); 
    OCR1AH = 0x3D; 
    OCR1AL = 0x08;
    TIMSK = (1<<OCIE1A); 
    
    // In CVAVR, configure the LCD to use PORTA for data pins (D4-D7)
    // and PORTC for control pins (RS=PC0, RW=PC1, E=PC2)
    lcd_init(16);
    lcd_clear();
    lcd_puts("E-Voting System");
    delay_ms(2000);


    // --- STEP 2: ADMIN LOGIN ---
    while (1) {
        lcd_clear();
        lcd_puts("Admin Password:");
        lcd_gotoxy(0, 1);
        readStringFromKeypad(keypadInput, 4);
        if (strcmp(keypadInput, adminPassword) == 0) {
            lcd_clear();
            lcd_puts("Access Granted!");
            delay_ms(1500);
            break; 
        } else {
            lcd_clear();
            lcd_puts("Wrong Password!");
            delay_ms(1500);
        }
    }


    // --- STEP 3: WAIT FOR VOTING TO START ---
    lcd_clear();
    lcd_puts("Press '*' to");
    lcd_gotoxy(0,1);
    lcd_puts("Start Voting");
    while (waitForKeypadPress() != '*');
    
    
    // --- STEP 4: RUN THE MAIN VOTING SESSION ---
    lcd_clear();
    lcd_puts("Voting Started!");
    delay_ms(1000);
    isVotingActive = 1; 
    #asm("sei")      

    while (isVotingActive == 1) { 
        updateSevenSegmentDisplay();
        
        if (totalVotesCast >= totalVoters || minutes >= voteTimeLimitMinutes) {
            isVotingActive = 0; 
            break; 
        }

        lcd_clear();
        lcd_puts("Enter Voter ID:");
        readStringFromKeypad(keypadInput, 4);
        
        voterFound = 0;
        voterDbPosition = -1;
        for (i = 0; i < totalVoters; i++) {
            if (strcmp(keypadInput, voterIdDatabase[i]) == 0) {
                if (hasVotedStatus[i] == 1) {
                    lcd_clear();
                    lcd_puts("Already Voted");
                    voterDbPosition = -2; 
                } else {
                    voterDbPosition = i; 
                }
                voterFound = 1;
                break; 
            }
        }
        if (voterFound == 0) {
            lcd_clear();
            lcd_puts("Invalid Voter ID");
        }

        if (voterDbPosition >= 0) {
            lcd_clear();
            lcd_puts("Use Buttons to Vote");
            
            while(1) {
                updateSevenSegmentDisplay();
                if (voteButtonPort.candidate1Button == 0) {
                    delay_ms(50);
                    if (voteButtonPort.candidate1Button == 0) {
                         candidateVotes[0]++;
                         while(voteButtonPort.candidate1Button == 0);
                         break;
                    }
                }
                if (voteButtonPort.candidate2Button == 0) {
                    delay_ms(50);
                    if (voteButtonPort.candidate2Button == 0) {
                        candidateVotes[1]++;
                        while(voteButtonPort.candidate2Button == 0);
                        break;
                    }
                }
                if (voteButtonPort.candidate3Button == 0) {
                    delay_ms(50);
                     if (voteButtonPort.candidate3Button == 0) {
                        candidateVotes[2]++;
                        while(voteButtonPort.candidate3Button == 0);
                        break;
                    }
                }
            }
            
            hasVotedStatus[voterDbPosition] = 1;
            totalVotesCast++;
            lcd_clear();
            lcd_puts("Thank You!");
            delay_ms(2000);
        } else {
            delay_ms(2000);
        }
    }


    // --- STEP 5: CALCULATE AND DISPLAY FINAL RESULTS ---
    highestVoteCount = 0;
    winnerCandidateIndex = -1;
    isATie = 0;

    for (j = 0; j < 3; j++) {
        if (candidateVotes[j] > highestVoteCount) {
            highestVoteCount = candidateVotes[j];
        }
    }
    
    candidatesWithMaxVotes = 0;
    for (j = 0; j < 3; j++) {
        if (candidateVotes[j] == highestVoteCount) {
            candidatesWithMaxVotes++;
            winnerCandidateIndex = j;
        }
    }
    
    if (candidatesWithMaxVotes > 1 || highestVoteCount == 0) {
        isATie = 1;
    }

    currentScreen = 0; 
    while (1) {
       for (k=0; k < 100; k++) {
           updateSevenSegmentDisplay();
       }
       
       currentScreen = !currentScreen;

       if (currentScreen == 0) {
           lcd_clear();
           lcd_puts("Voting Has Ended");
           lcd_gotoxy(0, 1);
           sprintf(lcdDisplayText, "C1:%d C2:%d C3:%d", candidateVotes[0], candidateVotes[1], candidateVotes[2]);
           lcd_puts(lcdDisplayText);
       } else {
           lcd_clear();
           if (isATie) {
               lcd_puts("Result: IT'S A TIE");
           } else {
               sprintf(lcdDisplayText, "Winner: Candidate %d", winnerCandidateIndex + 1);
               lcd_puts(lcdDisplayText);
           }
       }
    }
}


//******************************************************************************
// --- HELPER FUNCTIONS ---
//******************************************************************************

void updateSevenSegmentDisplay(void) {
    unsigned char s1, s2, m1, m2;
    char digitPatterns[10] = {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};

    s1 = seconds % 10;
    s2 = seconds / 10;
    m1 = minutes % 10;
    m2 = minutes / 10;
    
    // CORRECTED: Use PORTC for digit selection and PORTD for segment patterns
    digitPort &= 0x0F; // Clear the digit selection pins (PC4-PC7)
    
    digitPort.4 = 1; // Enable Digit 1 (controlled by PC4)
    segmentPort = digitPatterns[s1];
    delay_ms(5);
    digitPort.4 = 0;

    digitPort.5 = 1; // Enable Digit 2 (controlled by PC5)
    segmentPort = digitPatterns[s2];
    delay_ms(5);
    digitPort.5 = 0;
    
    digitPort.6 = 1; // Enable Digit 3 (controlled by PC6)
    segmentPort = digitPatterns[m1];
    delay_ms(5);
    digitPort.6 = 0;
    
    digitPort.7 = 1; // Enable Digit 4 (controlled by PC7)
    segmentPort = digitPatterns[m2];
    delay_ms(5);
    digitPort.7 = 0;
}

char waitForKeypadPress(void) {
    unsigned char row, col;
    const char keymap[4][3] = {
        {'1', '2', '3'}, {'4', '5', '6'}, {'7', '8', '9'}, {'*', '0', '#'}
    };
    while (1) {
        for (row = 0; row < 4; row++) {
            keypadPort = ~(1 << row); 
            delay_us(10); 
            if (keypadPin.4 == 0) { col = 0; }
            else if (keypadPin.5 == 0) { col = 1; }
            else if (keypadPin.6 == 0) { col = 2; }
            else { continue; }
            while (keypadPin.4 == 0 || keypadPin.5 == 0 || keypadPin.6 == 0);
            return keymap[row][col];
        }
    }
}

void readStringFromKeypad(char inputArray[], int len) {
    int i;
    for (i = 0; i < len; i++) {
        inputArray[i] = waitForKeypadPress();
        lcd_putchar(inputArray[i]);
    }
    inputArray[i] = '\0';
}