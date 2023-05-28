#include <AudioHacker.h>
#include <LiquidCrystal_I2C.h>

#define DEBUG
enum STATE {
    OFF,
    RECORDING,
    PLAYBACK,
    DIRECT_EFECTS,
    PASSTHROUGH
};
enum SELECTABLE_FLAGS {
    S_VOICE_CHANGER,
    S_ECHO,
    S_FUZZ,
    S_RECORD
};
enum EFECT {
    VOICE_CHANGER,
    ECHO,
    FUZZ
};
enum EFECT_STATE {
    OFF_S,
    ON_S
};

// define macros:
#define GRAINSIZE 512
#define MAX_ECHO 30720
#define EFECT_COUNT 3
#define SELECTABLES_COUNT 4
#define NEXT_SELECTABLE(E) (E == SELECTABLES_COUNT - 1) ? 0 : E + 1
#define PREV_SELECTABLE(E) (E == 0) ? SELECTABLES_COUNT - 1: E - 1
#define COMPUTE_ADDR(A) A.chip_nr * MAX_ADDR + A.addr
#define EQ_ADDR(A, B) (A.addr == B.addr) && (A.chip_nr == B.chip_nr)

// define inputs:
#define POT A0
#define POT_SWITCH 2
#define NEXT_B 3
#define TOGGLE_B 4
#define PREV_B 5
#define PLAYBACK_SWITCH 6


struct efect {
    bool state;
    int value;
};
struct mem_addr{
    unsigned long addr;
    unsigned int chip_nr;
};

bool nb_on, tb_on, pb_on, playback_on;
STATE state;
volatile unsigned int output = 2048;
unsigned int timer1Start;
unsigned int selected_efect = 0;
unsigned int record_buff;
unsigned int playback_buff[2];
mem_addr current_addr;
mem_addr start_addr, end_addr;
efect efect_lsit[EFECT_COUNT];

// Variabile pentru VoiceChanger
volatile unsigned int counter = 0;
byte counterMod = 2;
unsigned long lastDebugPrint = 0;
boolean normal = false;
volatile long readAddress = 0;
volatile long writeAddress = 0;
unsigned int buf[GRAINSIZE]; 

// Var pt. ECHO 
unsigned int echo_readBuf[2];
unsigned int echo_writeBuf;
boolean echo_evenCycle = true;
volatile long echo_address = 0;
unsigned int echoDelay;
boolean echoWrapped = false;

//Var pt FUZZ
int distortion = 0;
int d_sign = 1;


LiquidCrystal_I2C lcd(0x3F, 16, 2);

volatile bool odd_cycle;


void writeDisplay(const char * line_1 = NULL, const char * line_2 = NULL) {
    lcd.clear();                 
    lcd.setCursor(0, 0);         
    lcd.print(line_1);        

    if (line_2){
        lcd.setCursor(0, 1);         
        lcd.print(line_2); 
    }

    if (state == PLAYBACK) {
        lcd.setCursor(13, 1);       
        lcd.print("PB"); 
    }
}

const char * get_state_str(efect e) { 
    return (e.state) ? "ENABLED" : "DISSABLED"; 
}

const char * get_efect_name(int ef) {
    switch(ef) {
        case S_VOICE_CHANGER:
            return "VOICE CHANGER";
            break;
        case S_ECHO:
            return "ECHO";
            break;
        case S_FUZZ:
            return "BUZZ";
            break;
        case S_RECORD:
            return "RECORD";
            break;
        default:
            return "";
    }
}

bool check_button (int button, bool &pushed, int ttl = 200) {
    if (!digitalRead(button) && !pushed) {
        pushed = true;
        int t = millis();
        // debounce
        while (millis() - t < ttl) {
            if (digitalRead(button)) {
                pushed = false;
                return false;
            }
        }
        return true;
    } else if (digitalRead(button)) {
        pushed = false;
    }
    return false;
}

void setup() {
    #ifdef DEBUG
        Serial.begin(115200);
    #endif

    pinMode(POT_SWITCH      , INPUT_PULLUP);
    pinMode(PLAYBACK_SWITCH , INPUT_PULLUP);
    pinMode(NEXT_B          , INPUT_PULLUP);
    pinMode(TOGGLE_B        , INPUT_PULLUP);
    pinMode(PREV_B          , INPUT_PULLUP);

    state = DIRECT_EFECTS;
    timer1Start = UINT16_MAX -  (F_CPU / DEFAULT_RECORDING_SAMPLE_RATE);
    TCNT1 = timer1Start;
    

    lcd.init(); // initialize the lcd
    lcd.backlight(); 

    AudioHacker.begin();

    start_addr.addr = MAX_ECHO + 3;
    start_addr.chip_nr = 0;

    writeDisplay(get_efect_name(selected_efect), get_state_str(efect_lsit[selected_efect]));
}

void loop() {
    // Check "next" button
    if (check_button(NEXT_B, nb_on)) {
        selected_efect = NEXT_SELECTABLE(selected_efect);
        if (selected_efect == S_RECORD) {
            writeDisplay("RECORDING", "DISSABLED");
        } else {
            writeDisplay(get_efect_name(selected_efect), get_state_str(efect_lsit[selected_efect]));
        }
    }

    // Check "prev" button
    if (check_button(PREV_B, pb_on)) {
        selected_efect = PREV_SELECTABLE(selected_efect);
        if (selected_efect == S_RECORD) {
            writeDisplay("RECORDING", "DISSABLED");
        } else {
            writeDisplay(get_efect_name(selected_efect), get_state_str(efect_lsit[selected_efect]));
        }
    }

    // Check "toggle" button
    if (check_button(TOGGLE_B, tb_on, 500)) {
        #ifdef DEBUG
            Serial.print("TOGGLE: ");
        #endif
        // Check if we are in recording_mode
        if (selected_efect == S_RECORD && state != PLAYBACK) {
            #ifdef DEBUG
                Serial.println("RECORDING");
            #endif
            current_addr = start_addr;
            odd_cycle = true;
            state = RECORDING;
            writeDisplay("RECORDING", "ENABLED");
            while (digitalRead(TOGGLE_B) == LOW) { delay(10); }
            end_addr.chip_nr = current_addr.chip_nr;
            end_addr.addr = current_addr.addr;
            writeDisplay("RECORDING", "DISSABLED");
        } 
        // Switch the state of the efect
        if (selected_efect != S_RECORD) {
            efect_lsit[selected_efect].state = !efect_lsit[selected_efect].state;
            writeDisplay(get_efect_name(selected_efect), get_state_str(efect_lsit[selected_efect]));
            
            #ifdef DEBUG
                Serial.println(get_state_str(efect_lsit[selected_efect]));
            #endif
        }
    } else if ( selected_efect == S_RECORD && state == RECORDING ) {
        state = DIRECT_EFECTS;
        writeDisplay("RECORDING", "DISSABLED");
    }

    if ( digitalRead(POT_SWITCH) == LOW && selected_efect != S_RECORD ) {
        
        switch(selected_efect) {
            case VOICE_CHANGER:
                counterMod = map(analogRead(POT), 0, 1024, 2, 11);
                normal = (counterMod == 10);
                break;
            case ECHO:
                echoDelay = analogRead(0) * 30;
                break;
            case FUZZ:
                distortion = map(analogRead(0), 0, 1024, 0, 32);
                break;
            default:
                efect_lsit[selected_efect].value = map(-analogRead(POT), -1023, 0, 0, 8);
        }
    }

    if ( digitalRead(PLAYBACK_SWITCH) == LOW && state != PLAYBACK ) {
        current_addr = start_addr;
        state = PLAYBACK;

        if (selected_efect == S_RECORD ) {
            writeDisplay("RECORDING", "DISSABLED");
        } else {
            writeDisplay(get_efect_name(selected_efect), get_state_str(efect_lsit[selected_efect]));
        }

        #ifdef DEBUG
            Serial.println("PLAYBACK MODE ON.");
        #endif
    } else if ( digitalRead(PLAYBACK_SWITCH) == HIGH && state == PLAYBACK  ) {
        state = DIRECT_EFECTS;

        if (selected_efect == S_RECORD ) {
            writeDisplay("RECORDING", "DISSABLED");
        } else {
            writeDisplay(get_efect_name(selected_efect), get_state_str(efect_lsit[selected_efect]));
        }
    }
}

ISR(TIMER1_OVF_vect) {
    TCNT1 = timer1Start;
    
    unsigned int input;
    // Write out
    AudioHacker.writeDAC(output);

    // Acquaire sample
    if (state == PASSTHROUGH || state == DIRECT_EFECTS || state == RECORDING) {
        input = AudioHacker.readADC();
    } else if (state == PLAYBACK) {
        if (odd_cycle) {
            AudioHacker.readSRAMPacked(current_addr.chip_nr, current_addr.addr, playback_buff);
            input = playback_buff[0];
            current_addr.addr += 3;
            if (current_addr.addr > MAX_ADDR) {
                if (current_addr.chip_nr == 0) {
                    current_addr.addr = 0;
                    current_addr.chip_nr = 1;
                } else {
                    current_addr = start_addr;
                }
            }
            if (EQ_ADDR(current_addr, end_addr)) {
                current_addr = start_addr;
            }
        } else {
            input = playback_buff[1];
        }
        odd_cycle = !odd_cycle;
    }

    // process efects
    if (state == RECORDING) {
        if (odd_cycle) {
            record_buff = input;
        } else {
            // Write two samples to SRAM
            AudioHacker.writeSRAMPacked(current_addr.chip_nr, current_addr.addr, record_buff, input);

            current_addr.addr += 3;
            if (current_addr.addr > MAX_ADDR) {
                if (current_addr.chip_nr == 0) {
                    // proceed to the second SRAM chip
                    current_addr.addr = 0;
                    current_addr.chip_nr = 1;
                } else {
                    // end of memory, stop recording
                    state = PASSTHROUGH;
                }
            }
        }
        odd_cycle = !odd_cycle;
    }

    // bypass efects
    if (state == PASSTHROUGH || state == RECORDING) {
        output = input;
    } else {
        int tmp = input;
        // apply efects on the input
        if (efect_lsit[VOICE_CHANGER].state) {
            buf[writeAddress] = input;

            counter++;
            if (((counter % counterMod) != 0) || (normal)) {
                tmp = buf[readAddress];
                readAddress++;

                // cross-fade output if we are approaching the end of the grain
                // mix the output with the current realtime signal
                unsigned int distance = GRAINSIZE - writeAddress;
                if (distance <= 16) {
                    // weighted average of output and current input yields 16 bit number
                    unsigned int s = (output * distance) + (input * (16-distance)); 
                    s = s >> 4; // reduce to 12 bits.
                    tmp = s;
                }
            }

            writeAddress++;
            if (writeAddress >= GRAINSIZE) {
                writeAddress = 0; // loop around to beginning of grain
                readAddress = 0;
            }
        }
        else if (efect_lsit[ECHO].state) {
            unsigned int signal = input;
            unsigned int echo;
            int mix;

            if (echo_evenCycle) {
                long echoAddress = echo_address - echoDelay;
                if (echoAddress < 0) {
                    echoAddress += MAX_ECHO;
                }
                AudioHacker.readSRAMPacked(0, echoAddress, echo_readBuf);
                if ((!echoWrapped) && (echoAddress > echo_address)) {
                    // avoid reading from unwritten memory
                    echo = 2048;
                    echo_readBuf[1] = 2048;
                } else {
                    echo = echo_readBuf[0];
                }
            } else {
                echo = echo_readBuf[1];
            }
            if (echoDelay == 0) {
                echo = 2048;
            }

            if (echo_evenCycle) {
                echo_writeBuf = signal;
            } else {
                AudioHacker.writeSRAMPacked(0, echo_address, echo_writeBuf, signal);
                echo_address += 3;
                if (echo_address > MAX_ECHO) {
                    echo_address = 0;
                    echoWrapped = true;
                }
            }

            mix = signal-2048;
            echo = echo >> 1; // attenuate echo
            mix += (echo - 1024); // since we are dividing echo by 2, decrement by 1024
            if (mix < -2048) {
                mix = -2048;
            } else {
                if (mix > 2047) {
                    mix = 2047;
                }
            }
            tmp = mix + 2048;

            echo_evenCycle = !echo_evenCycle;
        }
        else if (efect_lsit[FUZZ].state) {
           int aux = (d_sign * (input >> 6) * distortion);
           tmp = input + aux;
           d_sign *= -1;
        }
        output = tmp;
    }

}
