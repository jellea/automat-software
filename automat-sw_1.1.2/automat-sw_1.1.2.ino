// EEPROM replacement Lib find in "Manage Libraries"and here https://github.com/cmaglie/FlashStorage
/*

V 1.1.2 – Untested user contribution by Justin

V 1.1.0

   Testplan:
   
   - Midi Speed
   - Connect both Din + USB and send a lot of data
   - Learn simple (single press button -  root node + chromatic up
   - Learn advanced (double press button - assign all notes in sequence
   -

*/

#include <FlashAsEEPROM.h>
#include <MIDI.h>
#include <MIDIUSB.h>
#include <SPI.h>
#include <OneButton.h>

// constants
const int OUTPUT_PINS_COUNT = 12;                       //= sizeof(OUTPUT_PINS) / sizeof(OUTPUT_PINS[0]);
const int LEARN_MODE_PIN = 38;                          // pin for the learn mode switch
const int SHIFT_REGISTER_ENABLE = 27;                   // Output enable for shiftregister ic
const int ACTIVITY_LED = 13;                            // activity led is still on D13 which is connected to PA17 > which means Pin 9 on MKRZero

// NV Data
typedef struct {
  byte   midiChannels[12];                                // 1-16 or 0 for any
  byte   midiPins[12];                                    // midi notes
  byte   alignfiller[8];                                  // for eeprom support
} dataCFG;
dataCFG nvData;


int velocity_program = 0;
int pwm_countdown[12];
int pwm_phase[12];
int pwm_kick[12];
int pwm_level[12];
const int COUNTDOWN_START = 14400;
const int NO_COUNTDOWN = 14401;
const int PHASE_KICK = 256;

/*
const int PHASE_LIMIT = 64;
const int DOWN_PHASE_MAX = 30;
const int LEVEL_MAX = 24;
const int VELOCITY_DIVISOR = 5;
*/

const int PHASE_LIMIT = 32;
const int DOWN_PHASE_MAX = 13;
const int LEVEL_MAX = 12;
const int VELOCITY_DIVISOR = 10;

FlashStorage(nvStore, dataCFG);

#include "solenoidSPI.h"
SOLSPI solenoids(&SPI, 30);                             // PB22 Pin in new layout is Pin14 on MKRZero

#include "dadaStatusLED.h"
dadaStatusLED statusLED(ACTIVITY_LED);                    // led controller

#include "dadaMidiLearn.h"                              // learn class

// Objects
OneButton button(LEARN_MODE_PIN, true);                 // 38 Pin in new layout is Pin 38 used for SD Card on MKRZero

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, midi2);   // DIN Midi Stuff
dadaMidiLearn midiLearn(&nvData);                       // lern class + load/save from eeprom

void setup() {
  Serial1.begin(31250);                                 // set up MIDI baudrate
  pinMode(SHIFT_REGISTER_ENABLE, OUTPUT);               // enable Shiftregister
  digitalWrite(SHIFT_REGISTER_ENABLE, LOW);
  pinMode(ACTIVITY_LED, OUTPUT);                        // pin leds to output
  pinMode(LEARN_MODE_PIN, INPUT_PULLUP);
  button.attachDoubleClick(doubleclick);                // register button for learnmodes
  button.attachClick(singleclick);                      // register button for learnmodes
  solenoids.begin();                                    // start shiftregister

  midi2.setHandleProgramChange(handleProgramChange);
  midi2.setHandleNoteOn(handleNoteOn);                  // add Handler for Din MIDI
  midi2.setHandleNoteOff(handleNoteOff);
  midi2.begin(MIDI_CHANNEL_OMNI);
  // init();
  statusLED.blink(20, 30, 32);
}

void loop() {
  midi2.read();
  button.tick();
  statusLED.tick();


  // handle blinking port on learning in advanced mode
  if(midiLearn.active) {
    if(midiLearn.mode==1) {
        solenoids.singlePin(midiLearn.counter,statusLED._state );
    }
  }

  if (velocity_program < 2) {
    // new single pulse width via velocity
    for(int i = 0 ; i < 12 ; i++){
      if(pwm_countdown[i] > 1 ){
          if(pwm_countdown[i] < NO_COUNTDOWN){
            pwm_countdown[i]--;
          }
          continue;
      }
      if(pwm_countdown[i] > 0) {
        solenoids.clearOutput(i);
        pwm_countdown[i]=0;
      }
    }
  } else if (velocity_program == 2) {
    // repeating pulse width via velocity
    for(int i = 0 ; i < 12 ; i++){
      if((pwm_countdown[i] == 0) || (pwm_level == 0)) {
          continue;
      }
      
      if(pwm_kick[i] > 0) {
        pwm_kick[i]--;
        continue;    
      }
      
      pwm_phase[i]--;
      pwm_countdown[i]--;
  
      if ((pwm_phase[i] == 0) || (pwm_countdown == 0)) {
        solenoids.setOutput(i);
  
        if (pwm_countdown == 0) {
          pwm_phase[i] = 0;              
        }
        else {
          pwm_phase[i] = PHASE_LIMIT;      
        }
      }
      else if (pwm_phase[i] == (DOWN_PHASE_MAX - pwm_level[i])) {
        solenoids.clearOutput(i);
      }
    }
  }



/*
  // constant PWM Out on Output 11
  static bool flag;
  static int timer_flag = 5;

  timer_flag--;
  if(timer_flag==0){

    timer_flag = 2700;
    
  flag = !flag;
  
  if(flag)  solenoids.setOutput(11);
   else   solenoids.clearOutput(11);
  }

*/


  
  // now handle usb midi and merge with DinMidi callbacks
  midiEventPacket_t rx;
  do {
    rx = MidiUSB.read();
    if (rx.header != 0) {
      switch (rx.byte1 & 0xF0) {
        case 0x90:  // note on
          if (rx.byte3 != 0)
            handleNoteOn(1 + (rx.byte1 & 0xF), rx.byte2, rx.byte3);
          else
            handleNoteOff(1 + (rx.byte1 & 0xF), rx.byte2, rx.byte3);
          break;
        case 0x80: // note off
          handleNoteOff(1 + (rx.byte1 & 0xF), rx.byte2, rx.byte3);
          break;
        case 0xC0: // program change
          handleProgramChange(1 + (rx.byte1 & 0xF), rx.byte2);
          break;
      }
    }
  } while (rx.header != 0);
}

/************************************************************************************************************************************************/
/************************************************************************************************************************************************/
/************************************************************************************************************************************************/
/************************************************************************************************************************************************/
/************************************************************************************************************************************************/
/************************************************************************************************************************************************/

void handleProgramChange(byte channel, byte patch) {
   velocity_program = patch;
   if (velocity_program > 3 || velocity_program < 0) {
      velocity_program = 0;
   }
  statusLED.blink(2, 1, 2); // LED Settings (On Time, Off Time, Count)
}

void handleNoteOn(byte channel, byte note, byte velocity) {
  midiLearn.noteOn(channel, note, velocity);

  if (midiLearn.active) {
    return;
  }

  statusLED.blink(1, 2, 1);

  
  for (int i = 0 ; i < 12 ; i++) {
    if (nvData.midiPins[i] == note) {
      if (nvData.midiChannels[i] == channel || nvData.midiChannels[i] == 0) {
        solenoids.setOutput(i);

        switch (velocity_program) 
        {
            case 0:  // strategy from 1.1.0  quadratic
              pwm_countdown[i] = velocity * velocity; // set velocity timer
              break;
            case 1: // inverse quadratic
              if (velocity < 120)
              {
                 velocity = 120 - velocity;
                 pwm_countdown[i] = COUNTDOWN_START - ((velocity * velocity) * 7/ 8);
              }
              else
              {
                pwm_countdown[i] = NO_COUNTDOWN;
              }
              break;
            case 2: // true pwm
              pwm_level[i] = (velocity / VELOCITY_DIVISOR) + 1;
              if(pwm_level[i] > LEVEL_MAX) {
                pwm_countdown[i] = 0;
                pwm_phase[i] = 0;
                pwm_kick[i] = 0;
                pwm_level[i] = 0;
              }
              else {
                pwm_countdown[i] = COUNTDOWN_START; 
                pwm_phase[i] = PHASE_LIMIT;
                pwm_kick[i] = PHASE_KICK;
              }
              break;
            default: // no velocity control
             break;
        }
      }
    }
  }

}

void handleNoteOff(byte channel, byte note, byte velocity) {
  midiLearn.noteOff(channel, note, velocity);

  if (midiLearn.active) {
    return;
  }
  
  statusLED.blink(1, 2, 1);
  
  for (int i = 0 ; i < 12 ; i++) {
    if (nvData.midiPins[i] == note) {
      if (nvData.midiChannels[i] == channel || nvData.midiChannels[i] == 0) {
        solenoids.clearOutput(i);
        pwm_countdown[i] = 0;
        pwm_kick[i] = 0;
        pwm_phase[i] = 0;
        pwm_level[i] = 0;
      }
    }
  }
}

// Advanced Learn
void doubleclick() {
  statusLED.blink(10, 10, -1);  // LED Settings (On Time, Off Time, Count)
  midiLearn.begin(1);
}

// Simple Learn
void singleclick(void)  {
  statusLED.blink(10, 0, -1); // LED Settings (On Time, Off Time, Count)
  midiLearn.begin(0);
}


