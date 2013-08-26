#include <SPI.h>

#define LED_COUNT       50   // If LED_COUNT > 255, promote counter to int
#define BYTES_PER_COLOR 3    // Only applies to color data, not output. Code needs to be rewritten if you change this
#define MAX_COLORS      85   // If MAX_COLORS*BYTES_PER_COLOR > 255, promote col_idx to int 
#define MAX_AMPLITUDES  85   // If MAX_AMPLITUDES*BYTES_PER_COLOR > 255, promote out_idx and output_bytes to int
                             // If MAX_AMPLITUDES > 255 or MAX_COLORS > 255 protocol needs to be modified

enum { ERROR_BYTE      = 'e',  // Something went wrong: device is now in command mode          -> 'm'
       MODE_COMMAND    = 'm',  // Next byte is a command                                       -> mode from byte
       FLAG_OFFSET_ON  = 'O',  // Expect beat offset at the start of amp bytes.                -> 'm'
       FLAG_OFFSET_OFF = 'o',  // Do not expect beat offset at the start of amp bytes          -> 'm'
       MODE_INIT_COLOR = 'c',  // Receive color data - Next byte is a color byte               -> 'C' or 'm'
       MODE_GET_COLORS = 'C',  // INTERNAL: Receive color data - Next byte is a color byte     -> 'C' or 'm'
       MODE_INIT_AMPS  = 'a',  // Init amplitude data - Next byte is the number of amplitudes  -> 'K' or 'A' or 'm'
       MODE_GET_OFFSET = 'K',  // INTERNAL: Receive offset number - Next byte                  -> 'A'
       MODE_GET_AMPS   = 'A'}; // INTERNAL: Receive amplitude data - Next byte is an amplitude -> 'A' or 'a'

inline boolean checkMode( byte proposedMode ) {
  return    proposedMode == MODE_COMMAND
         || proposedMode == MODE_INIT_COLOR
      // || proposedMode == MODE_GET_COLORS
         || proposedMode == MODE_INIT_AMPS
      // || proposedMode == MODE_GET_OFFSET
      // || proposedMode == MODE_GET_AMPS
         ;
}

inline boolean checkCommand( byte proposedCommand ) {
  return    proposedCommand == FLAG_OFFSET_ON
         || proposedCommand == FLAG_OFFSET_OFF
         ;
}

byte mode;
byte counter;
byte out_idx;
byte col_idx;
byte colors[MAX_COLORS*BYTES_PER_COLOR];
byte output[MAX_AMPLITUDES * 2];
byte colors_len;
byte amplitudes_len;
byte offset;
boolean expectOffset;

//boolean flip; // Used for crude a/b profiling

void setup()
{
  SPI.setClockDivider(SPI_CLOCK_DIV32);
  SPI.begin();
  SPI.transfer(0); 
  SPI.transfer(0);
  for (int i = 0; i <  LED_COUNT; ++i) {
    SPI.transfer(0x80);
    SPI.transfer(0x00); 
  }
  SPI.transfer(0); 
  SPI.transfer(0);
  SPI.end();
  Serial.begin(115200);
  counter = 0;
  mode = MODE_COMMAND;
  colors_len = 0;
  amplitudes_len = 0;
  offset = 0;
  expectOffset = false;
  //flip = false;
  
  Serial.write(MODE_COMMAND);
}

void loop() {
  byte inByte;
  int mungedByte;
  
  mungedByte = Serial.read(); 
  if (mungedByte >= 0) {
    inByte = lowByte(mungedByte);
    
    switch (mode) {
      case MODE_COMMAND:
        if (checkMode(inByte)) {
          mode = inByte;
          counter = 0;
          Serial.write(inByte);
        }
        else if (checkCommand(inByte)) {
          switch (inByte) {
            case FLAG_OFFSET_ON:
              expectOffset = true;
              break;
            case FLAG_OFFSET_OFF:
              expectOffset = false;
              break;
          }
        }
        else {
          Serial.write(ERROR_BYTE);
          mode = MODE_COMMAND;
        }
        break;
      case MODE_INIT_COLOR:
        if (inByte == 0) {
          mode = MODE_COMMAND;
          Serial.write(MODE_COMMAND);
        }
        else {
          colors_len = inByte * BYTES_PER_COLOR;
          if (colors_len > MAX_AMPLITUDES * BYTES_PER_COLOR) {
            Serial.write(ERROR_BYTE);
            mode = MODE_COMMAND;
          }
          else {
            mode = MODE_GET_COLORS;
          }
        }
        break;
      case MODE_GET_COLORS:
        colors[counter] = inByte;
        ++counter;
        if (counter >= colors_len) {
          counter = 0;
          mode = MODE_INIT_COLOR;
          Serial.write(MODE_INIT_COLOR);
        }
        break;
      case MODE_INIT_AMPS:
        if (inByte == 0) {
          mode = MODE_COMMAND;
          Serial.write(MODE_COMMAND);
        }
        else {
          amplitudes_len = inByte;
          if (amplitudes_len > MAX_AMPLITUDES || amplitudes_len > colors_len) {
            Serial.write(ERROR_BYTE);
            amplitudes_len = 0;
            mode = MODE_COMMAND;
          }
          else {
            SPI.begin();
            SPI.transfer(0); 
            SPI.transfer(0);
            out_idx = 0;
            col_idx = 0;
            counter = 0;
            if (expectOffset)
              mode = MODE_GET_OFFSET;
            else
              mode = MODE_GET_AMPS;
              
            //Serial.write(MODE_INIT_AMPS);
          }
        }
        break;
      case MODE_GET_OFFSET:
        offset = inByte;
        mode = MODE_GET_AMPS;
        //Serial.write(MODE_INIT_AMPS);
        break;  
      case MODE_GET_AMPS:
        if (counter < amplitudes_len) {
          byte r = highByte(inByte * colors[col_idx++]);
          byte g = highByte(inByte * colors[col_idx++]);
          byte b = highByte(inByte * colors[col_idx++]);
          
          byte hb = rgbTo15bit_h(r, g);
          byte lb = rgbTo15bit_l(g, b); 

          output[out_idx++] = hb;
          output[out_idx++] = lb;
          ++counter;
        }
        if (counter >= amplitudes_len) {
          byte output_bytes = out_idx;
          out_idx = offset*2;
          counter = 0;
          while (counter < LED_COUNT) {
            SPI.transfer(output[out_idx++]);
            SPI.transfer(output[out_idx++]);
            out_idx = (out_idx>=output_bytes)?(out_idx-output_bytes):out_idx;
            ++counter;
          }
          SPI.transfer(0); 
          SPI.transfer(0);
          SPI.end();
            
          //flip = !flip;
          mode = MODE_INIT_AMPS;
          Serial.write(MODE_INIT_AMPS);
        }
        break;
      default:
        Serial.write(ERROR_BYTE);
        mode = MODE_COMMAND;
        break;
    } 
  }
}

inline byte rgbTo15bit_h(byte r, byte g) {
  return ((r>>1U & 0x7CU) | (g>>6U)) | 0x80U; 
} // ((r & 0xF8U) >> 1U) | (g>>6U) | 0x80U   is clearer, but avr-gcc generates a spurious promotion to int
  //                                         that I can't seem to hint away. Was it worth it? Not really...


inline byte rgbTo15bit_l(byte g, byte b) {
  return ((g & 0xf8U)<<2U) | (b >> 3U);
}
