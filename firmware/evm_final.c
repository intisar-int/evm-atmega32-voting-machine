#include <mega32.h>
#include <delay.h>
#include <stdio.h>
#include <string.h>
#include <alcd.h>

// ---CONFIGURATION---
#define adminPass "1357"
#define voterLimit 5
#define timeLimit 2
#define idLen 4

// ---HARDWARE DEFINITIONS---
#define keyPort PORTB
#define keyDdr DDRB
#define keyPin PINB

#define btnPort PINA

#define btnDdr DDRA

// ---GLOBAL VARIABLES---
char voterDb[voterLimit][idLen] = {
    {'1','1','1','1'},
    {'2','2','2','2'},
    {'3','3','3','3'},
    {'4','4','4','4'},
    {'5','5','5','5'}
};
unsigned char votedFlags[voterLimit] = {0, 0, 0, 0, 0};
unsigned int votes[3] = {0, 0, 0};
unsigned char seconds = 0;
unsigned char minutes = 0;
unsigned char votingOn = 0;
unsigned char isPaused = 0;
unsigned char timeoutCounter = 0;

void displayTimeOnLcd(void){
    char timeBuffer[6];
    sprintf(timeBuffer, "%02u:%02u", minutes, seconds);
    lcd_gotoxy(11, 1);
    lcd_puts(timeBuffer);
}

char scanKeypad(void) {
    unsigned char row, col;
    const char keymap[4][3] = {
        {'1', '2', '3'}, {'4', '5', '6'}, {'7', '8', '9'}, {'*', '0', '#'}
    };

    for (row = 0; row < 4; row++) {
        keyPort = ~(1 << row);
        delay_us(10);

        if (keyPin.4 == 0) { col = 0; }
        else if (keyPin.5 == 0) { col = 1; }
        else if (keyPin.6 == 0) { col = 2; }
        else { continue; }

        return keymap[row][col];
    }

    return 0;
}

char waitForKeyPress(void) {
    char key = 0;
    while( (key = scanKeypad()) == 0 );
    while( scanKeypad() != 0 );
    return key;
}

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
    if (timeoutCounter > 0) {
        timeoutCounter--;
    }
}

void main(void)
{

    char lcdBuffer[17];
    char currentInput[idLen];
    int voterIndex;
    unsigned char votesCast = 0;
    int i, j;
    int maxVotes;
    int winnerIndex;
    unsigned char isTie;
    int maxCount;
    unsigned char screenNum;
    char key;

    unsigned char inputIndex;
    unsigned char lastSecond;
    unsigned char passCorrect;
    unsigned char passIndex;
    unsigned char match;

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

    while (1) {
        lcd_clear();
        lcd_puts("Admin Password:");
        lcd_gotoxy(0, 1);

        passCorrect = 1;
        for (i = 0; i < idLen; i++) {
            key = waitForKeyPress();
            lcd_putchar(key);
            if (key != adminPass[i]) {
                passCorrect = 0;
            }
        }

        if (passCorrect == 1) {
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

    lcd_clear();
    lcd_puts("Press '*' to");
    lcd_gotoxy(0,1);
    lcd_puts("Start Voting");
    while (waitForKeyPress() != '*');

    lcd_clear();
    lcd_puts("Enter ID or # to Pause");

    minutes = timeLimit;
    seconds = 0;
    lastSecond = 99;

    votingOn = 1;
    #asm("sei")

    inputIndex = 0;
    while (votingOn == 1) {
        if (seconds != lastSecond) {
            displayTimeOnLcd();
            lastSecond = seconds;
        }

        if (votesCast >= voterLimit || (minutes == 0 && seconds == 0)) {
            votingOn = 0;
            break;
        }

        key = scanKeypad();

        if (key != 0) {
            // --- PAUSE LOGIC ---
            if (key == '#') {
                while(scanKeypad() != 0); // Wait for '#' key release

                lcd_clear();

                lcd_puts("Enter Pass");
                displayTimeOnLcd();

                timeoutCounter = 30;
                passIndex = 0;
                passCorrect = 1;

                while (passIndex < idLen && timeoutCounter > 0) {
                    key = scanKeypad();
                    if (key != 0) {
                        lcd_gotoxy(passIndex, 1);
                        lcd_putchar('*');
                        if (key != adminPass[passIndex]) {
                            passCorrect = 0;
                        }
                        passIndex++;
                        while(scanKeypad() != 0);
                    }
                }

                timeoutCounter = 0;

                if (passIndex < idLen) {
                    lcd_clear();
                    lcd_puts("Timeout!");
                    delay_ms(1500);
                } else if (passCorrect == 1) {
                    isPaused = 1;
                    lcd_clear();
                    lcd_puts("System Paused");
                    lcd_gotoxy(0,1);
                    lcd_puts("Press '*' to Resume");
                    while(waitForKeyPress() != '*');
                    isPaused = 0;
                } else {
                    lcd_clear();
                    lcd_puts("Wrong Password!");
                    delay_ms(1500);
                }

                inputIndex = 0;
                lcd_clear();
                lcd_puts("Enter ID or # to Pause");
                continue;

            }

            if (inputIndex < idLen) {
                currentInput[inputIndex] = key;
                lcd_gotoxy(inputIndex, 1);
                lcd_putchar(key);
                inputIndex++;
                while(scanKeypad() != 0);
            }

            if (inputIndex >= idLen) {
                voterIndex = -1;
                for (i = 0; i < voterLimit; i++) {
                    match = 1;
                    for (j = 0; j < idLen; j++) {
                        if (currentInput[j] != voterDb[i][j]) {
                            match = 0;
                            break;
                        }
                    }
                    if (match == 1) {
                        if (votedFlags[i] == 1) voterIndex = -2;
                        else voterIndex = i;
                        break;
                    }
                }

                if (voterIndex == -1) { lcd_clear(); lcd_puts("Invalid ID"); }
                else if (voterIndex == -2) { lcd_clear(); lcd_puts("Already Voted"); }

                if (voterIndex >= 0) {
                    lcd_clear();
                    lcd_puts("Use Buttons to Vote");

                    while(1) {
                        if (seconds != lastSecond) { displayTimeOnLcd(); lastSecond = seconds; }
                        if (PINA.0 == 0) { votes[0]++; while(PINA.0 == 0); break; }
                        if (PINA.1 == 0) { votes[1]++; while(PINA.1 == 0); break; }
                        if (PINA.2 == 0) { votes[2]++; while(PINA.2 == 0); break; }
                    }

                    votedFlags[voterIndex] = 1;
                    votesCast++;

                    lcd_clear();
                    lcd_puts("Thank You!");
                    delay_ms(1500);
                } else {
                    delay_ms(1500);
                }

                inputIndex = 0;
                lcd_clear();
                lcd_puts("Enter ID or # to Pause");
            }
        }
    }

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
            lcd_clear();
            lcd_puts("---Final Scores---");
            lcd_gotoxy(0, 1);
            sprintf(lcdBuffer, "BNP:%-4d JMT:%-4d", votes[0], votes[1]);
            lcd_puts(lcdBuffer);
            lcd_gotoxy(0,1);
            sprintf(lcdBuffer, "NCP:%d", votes[2]);
            lcd_puts(lcdBuffer);
        } else {
            lcd_clear();
            lcd_puts("---Result---");
            lcd_gotoxy(0,1);
            if (isTie) lcd_puts("IT'S A TIE");
            else {
                if (winnerIndex == 0) sprintf(lcdBuffer, "Winner: BNP");

                else if (winnerIndex == 1) sprintf(lcdBuffer, "Winner: JMT");
                else sprintf(lcdBuffer, "Winner: NCP");
                lcd_puts(lcdBuffer);
            }
        }
        screenNum = !screenNum;
        delay_ms(4000);
    }
}
