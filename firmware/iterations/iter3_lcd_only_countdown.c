#include <mega32.h>
#include <delay.h>
#include <stdio.h>
#include <string.h>
#include <alcd.h>

// ---CONFIGURATION---
#define adminPass "1357"
#define voterLimit 5
#define timeLimit 2
#define idLen 5 

#define keyPort PORTB
#define keyDdr  DDRB
#define keyPin  PINB

#define btnPort PINA
#define btnDdr  DDRA

// ---GLOBAL VARIABLES---
char voterDb[voterLimit][idLen] = {
    "1111", "2222", "3333", "4444", "5555"
};
unsigned char votedFlags[voterLimit] = {0, 0, 0, 0, 0};
unsigned int votes[3] = {0, 0, 0}; // Index 0: BNP, Index 1: JMT, Index 2: NCP
unsigned char seconds = 0;
unsigned char minutes = 0;
unsigned char votingOn = 0;
unsigned char isPaused = 0;

// ---HELPER FUNCTION PROTOTYPES---
char getKey(void);
void readString(char inArr[], int len);
int readVoterInput(char inArr[], int len);
void displayTimeOnLcd(void);

// ---TIMER INTERRUPT---
interrupt [TIM1_COMPA] void timer1_compa_isr(void)
{
    if (votingOn == 1 && isPaused == 0) {
        if (seconds == 0) {
            if (minutes == 0) {
                votingOn = 0; 
            } else {
                minutes--;
                seconds = 59;
            }
        } else {
            seconds--;
        }
    }
}

//******************************************************************************
// --- MAIN PROGRAM ---
//******************************************************************************
void main(void)
{
    // --- LOCAL VARIABLES ---
    char keyInput[idLen];
    char lcdBuffer[17];
    int voterIndex;
    unsigned char votesCast = 0;
    int i, j;
    int found;
    int maxVotes;
    int winnerIndex;
    unsigned char isTie;
    int maxCount;
    unsigned char screenNum;

    // --- STEP 1: INITIALIZE THE SYSTEM ---
    btnDdr = 0xF0; 
    btnPort = 0x07;           
    keyDdr = 0x0F;      
    keyPort = 0xF0;     
    DDRC = 0xFF; 
    
    TCCR1A = 0x00;
    TCCR1B = (1<<WGM12) | (1<<CS12) | (1<<CS10); 
    OCR1AH = 0x3D; 
    OCR1AL = 0x08;
    TIMSK = (1<<OCIE1A); 
    
    lcd_init(16);
    lcd_clear();
    lcd_puts("Nirbachon-2026");
    delay_ms(2000);

    // --- STEP 2: ADMIN LOGIN ---
    while (1) {
        lcd_clear();
        lcd_puts("Admin Password:");
        lcd_gotoxy(0, 1);
        readString(keyInput, 4); 
        if (strcmp(keyInput, adminPass) == 0) {
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
    while (getKey() != '*');
    
    // --- STEP 4: RUN THE MAIN VOTING SESSION ---
    lcd_clear();
    lcd_puts("Voting Started!");
    delay_ms(1000);
    
    minutes = timeLimit;
    seconds = 0;
    
    votingOn = 1; 
    #asm("sei")      

    while (votingOn == 1) { 
        if (votesCast >= voterLimit || (minutes == 0 && seconds == 0)) {
            votingOn = 0;
            break; 
        }

        lcd_clear();
        lcd_puts("Input ID ");
        displayTimeOnLcd(); 
        
        if (readVoterInput(keyInput, 4) == 1) {
            isPaused = 1;
            lcd_clear();
            lcd_puts("PAUSE: Enter Pass");
            displayTimeOnLcd();
            readString(keyInput, 4);
            
            if (strcmp(keyInput, adminPass) == 0) {
                lcd_clear();
                lcd_puts("System Paused");
                lcd_gotoxy(0,1);
                lcd_puts("Press '*' to Resume");
                
                while(getKey() != '*'){
                    displayTimeOnLcd();
                }
                isPaused = 0; 
                lcd_clear();
                lcd_puts("Resuming...");
                delay_ms(1500);
            } else {
                lcd_clear();
                lcd_puts("Wrong Password!");
                isPaused = 0; 
                delay_ms(1500);
            }
            continue; 
        }
        
        found = 0;
        voterIndex = -1;
        for (i = 0; i < voterLimit; i++) {
            if (strcmp(keyInput, voterDb[i]) == 0) {
                if (votedFlags[i] == 1) {
                    voterIndex = -2; 
                } else {
                    voterIndex = i; 
                }
                found = 1;
                break; 
            }
        }
        
        if (found == 0) {
             lcd_clear();
             lcd_puts("Invalid Voter ID");
             displayTimeOnLcd();
        } else if (voterIndex == -2) {
             lcd_clear();
             lcd_puts("Already Voted");
             displayTimeOnLcd();
        }

        if (voterIndex >= 0) {
            lcd_clear();
            lcd_puts("Vote: 1-BNP 2-JMT 3-NCP");
            displayTimeOnLcd();
            
            while(1) {
                if (PINA.0 == 0) {
                    delay_ms(50); 
                    if (PINA.0 == 0) {
                        votes[0]++; // Vote for BNP
                        while(PINA.0 == 0); 
                        break;
                    }
                }
                if (PINA.1 == 0) {
                    delay_ms(50);
                    if (PINA.1 == 0) {
                        votes[1]++; // Vote for JMT
                        while(PINA.1 == 0);
                        break;
                    }
                }
                if (PINA.2 == 0) {
                    delay_ms(50);
                    if (PINA.2 == 0) {
                        votes[2]++; // Vote for NCP
                        while(PINA.2 == 0);
                        break;
                    }
                }
            }
            
            votedFlags[voterIndex] = 1;
            votesCast++;
            lcd_clear();
            lcd_puts("Thank You!");
            displayTimeOnLcd();
            delay_ms(2000);
        } else {
            delay_ms(2000);
        }
    }

    // --- STEP 5: CALCULATE AND DISPLAY FINAL RESULTS ---
    lcd_clear();
    lcd_puts("Voting Ended!");
    delay_ms(2000);

    maxVotes = 0;
    winnerIndex = -1;
    isTie = 0;
    for (j = 0; j < 3; j++) { if (votes[j] > maxVotes) maxVotes = votes[j]; }
    maxCount = 0;
    for (j = 0; j < 3; j++) { if (votes[j] == maxVotes) { maxCount++; winnerIndex = j; } }
    if (maxCount > 1 || maxVotes == 0) isTie = 1;

    screenNum = 0; 
    while (1) {
       if (screenNum == 0) {
           // UPDATED: Show final scores with names
           lcd_clear();
           sprintf(lcdBuffer, "BNP:%-4d JMT:%-4d", votes[0], votes[1]);
           lcd_puts(lcdBuffer);
           lcd_gotoxy(0, 1);
           sprintf(lcdBuffer, "NCP:%d", votes[2]);
           lcd_puts(lcdBuffer);
       } else {
           lcd_clear();
           lcd_puts("---Result---");
           lcd_gotoxy(0,1);
           if (isTie) {
               lcd_puts("IT'S A DRAW");
           } else {
               // UPDATED: Show winner's name instead of number
               if (winnerIndex == 0) {
                   sprintf(lcdBuffer, "Winner: BNP");
               } else if (winnerIndex == 1) {
                   sprintf(lcdBuffer, "Winner: JMT");
               } else {
                   sprintf(lcdBuffer, "Winner: NCP");
               }
               lcd_puts(lcdBuffer);
           }
       }
       screenNum = !screenNum;
       delay_ms(4000);
    }
}

//******************************************************************************
// --- HELPER FUNCTIONS ---
//******************************************************************************

void displayTimeOnLcd(void){
    char timeBuffer[6];
    sprintf(timeBuffer, "%02u:%02u", minutes, seconds);
    lcd_gotoxy(11, 1); 
    lcd_puts(timeBuffer);
}

char getKey(void) {
    unsigned char row, col;
    const char keymap[4][3] = {
        {'1', '2', '3'}, {'4', '5', '6'}, {'7', '8', '9'}, {'*', '0', '#'}
    };
    while (1) {
        for (row = 0; row < 4; row++) {
            keyPort = ~(1 << row); 
            delay_us(10); 
            if (keyPin.4 == 0) { col = 0; }
            else if (keyPin.5 == 0) { col = 1; }
            else if (keyPin.6 == 0) { col = 2; }
            else { continue; }
            while (keyPin.4 == 0 || keyPin.5 == 0 || keyPin.6 == 0);
            return keymap[row][col];
        }
    }
}

void readString(char inArr[], int len) {
    int i;
    for (i = 0; i < len; i++) {
        inArr[i] = getKey();
        lcd_putchar(inArr[i]);
    }
    inArr[i] = '\0';
}

int readVoterInput(char inArr[], int len) {
    int i;
    char key;
    lcd_gotoxy(0,1);
    for (i = 0; i < len; i++) {
        key = getKey();
        if (key == '#') {
            return 1;
        }
        inArr[i] = key;
        lcd_putchar(key);
    }
    inArr[i] = '\0';
    return 0;
}