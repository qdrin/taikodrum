#include "AnalogReadNow.h"

//#define DEBUG_OUTPUT
//#define DEBUG_OUTPUT_LIVE
// #define DEBUG_TIME
// #define DEBUG_DATA
#define HID_KEYBOARD

#define KEY_ESC 0x29   // Keyboard ESCAPE

uint8_t buf[8] = { 
  0 };   /* Keyboard report buffer */

uint8_t keys_pressed[6] = {0, 0, 0, 0, 0, 0};
int keys_pressed_counter = 0;
const int keys[4] = {
  0x07,  // Keyboard d and D
  0x09,  // Keyboard f and F
  0x0d,  // Keyboard j and J
  0x0e  // Keyboard k and K
  };
const char char_keys[4] = {
  'd', 'f', 'j', 'k'
};

const int pins[4] = {
  A1,  // d
  A0,  // f
  A3,  // j
  A2   // k
  };

const int min_threshold = 15;
const long cd_length = 10000;
const float k_threshold = 1.5;
const float k_decay = 0.97;

const float sens[4] = {1.0, 1.0, 1.0, 1.0};

const int key_next[4] = {3, 2, 0, 1};

const long cd_stageselect = 200000;

bool stageselect = false;
bool stageresult = false;

float threshold = 20;
int raw[4] = {0, 0, 0, 0};
float level[4] = {0, 0, 0, 0};
long cd[4] = {0, 0, 0, 0};
bool down[4] = {false, false, false, false};

typedef unsigned long time_t;
time_t t0 = 0;
time_t dt = 0, sdt = 0;

void sample() {
  int prev[4] = {raw[0], raw[1], raw[2], raw[3]};
  raw[0] = analogRead(pins[0]);
  raw[1] = analogRead(pins[1]);
  raw[2] = analogRead(pins[2]);
  raw[3] = analogRead(pins[3]);
  for (int i=0; i<4; ++i)
    level[i] = abs(raw[i] - prev[i]) * sens[i];
}

void sampleSingle(int i) {
  int prev = raw[i];
  raw[i] = analogReadNow();
  level[i] = abs(raw[i] - prev) * sens[i];
  analogSwitchPin(pins[key_next[i]]);
}

void releaseKey(int k) 
{
  buf[0] = 0;
  for(int i=0; i < keys_pressed_counter; i++) {
    if(keys_pressed[i] == k) {
      for(int j = i; j < keys_pressed_counter - 1; j++)
        keys_pressed[j] = keys_pressed[j+1];
      keys_pressed[keys_pressed_counter--] = 0;
    }
  }
  for(int i=keys_pressed_counter; i < 6; i++)
    keys_pressed[i] = 0;
  for(int i = 2; i < 8; i++)
    buf[i] = keys_pressed[i-2];
#ifdef HID_KEYBOARD
  Serial.write(buf, 8); // Release key
// #else
//   Serial.println();
//   Serial.print("after release ");
//   Serial.print(k);
//   Serial.print(" keys_pressed_counter=");
//   Serial.print(keys_pressed_counter);
//   Serial.println();
#endif  
}

void pressKey(int k)
{
  keys_pressed[keys_pressed_counter] = k;
  buf[2 + keys_pressed_counter++] = k;
#ifdef HID_KEYBOARD
  Serial.write(buf, 8);
#else
  for(int i = 0; i < 4; i++) {
    if(keys[i] == k)
      Serial.print(char_keys[i]);
  }
#endif
}

void setup() {
  analogReference(DEFAULT);
  analogSwitchPin(pins[0]);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  t0 = micros();
  Serial.begin(9600);
}

void parseSerial() {
  static char command = -1;
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (command == -1)
      command = c;
    else {
      switch (command) {
      case 'C':
        Serial.write('C');
        Serial.write(c);
        Serial.flush();
        break;
      case 'S':
        stageselect = (c == '1');
        digitalWrite(LED_BUILTIN, stageselect ? HIGH : LOW);
        break;
      case 'R':
        stageresult = (c == '1');
        digitalWrite(LED_BUILTIN, stageresult ? HIGH : LOW);
        break;
      }
      command = -1;
    }
  }
}

void loop_test() {
  sampleSingle(0);
  Serial.print(level[0]);
  Serial.print("\t");
  delayMicroseconds(500);
  sampleSingle(1);
  Serial.print(level[1]);
  Serial.print("\t");
  delayMicroseconds(500);
  sampleSingle(2);
  Serial.print(level[2]);
  Serial.print("\t");
  delayMicroseconds(500);
  sampleSingle(3);
  Serial.print(level[3]);
  Serial.println();
  delayMicroseconds(500);
}

void loop_test2() {
  int res = 0;
  int printed = 0;
  for(int i = 0; i < 4; i++) {
    res = analogRead(pins[i]);
    if(res > min_threshold) {
      Serial.print(i);
      Serial.print(": ");
      Serial.print(res);
      Serial.print(", ");
      printed = 1;
      delayMicroseconds(500);
    }
  }
  if(printed)
    Serial.println();
}

void loop() {
  // loop_test2(); return;
  
  static int si = 0;
  
  time_t t1 = micros();
  dt = t1 - t0;
  sdt += dt;
  t0 = t1;
  
  float prev_level = level[si];
  sampleSingle(si);
  float new_level = level[si];
  level[si] = (level[si] + prev_level * 2) / 3;
  
  threshold *= k_decay;

  for (int i = 0; i != 4; ++i) {
    if (cd[i] > 0) {
      cd[i] -= dt;
      if (cd[i] <= 0) {
        cd[i] = 0;
        if (down[i]) {
          releaseKey(stageresult ? KEY_ESC : keys[i]);
          down[i] = false;
        }
      }
    }
  }
  
  int i_max = 0;
  int level_max = 0;
  
  for (int i = 0; i != 4; ++i) {
    if (level[i] > level_max && level[i] > threshold) {
      level_max = level[i];
      i_max = i;
    }
  }

  if (i_max == si && level_max >= min_threshold) {
    if (cd[i_max] == 0) {
      if (!down[i_max]) {
#ifdef DEBUG_DATA
        for(int i = 0; i < 4; i++) {
          Serial.print(level[i], 1);
          Serial.print("\t");
        }
        Serial.println();
#endif
        if (stageresult) {
          pressKey(KEY_ESC);
        } else {
          pressKey(keys[i_max]);
        }
        down[i_max] = true;
      }
      for (int i = 0; i != 4; ++i)
        cd[i] = cd_length;
      if (stageselect)
        cd[i_max] = cd_stageselect;
    }
    sdt = 0;
  }

  if (cd[i_max] > 0) {
    threshold = max(threshold, level_max * k_threshold);
  }
  
  static time_t ct = 0;
  static int cc = 0;
  ct += dt;
  cc += 1;

#ifdef DEBUG_OUTPUT
  static bool printing = false;
#ifdef DEBUG_OUTPUT_LIVE
  if (true)
#else
  if (printing || (/*down[0] &&*/ threshold > 10))
#endif
  {
    printing = true;
    Serial.print(level[0], 1);
    Serial.print("\t");
    Serial.print(level[1], 1);
    Serial.print("\t");
    Serial.print(level[2], 1);
    Serial.print("\t");
    Serial.print(level[3], 1);
    Serial.print("\t| ");
    Serial.print(cd[0] == 0 ? "  " : down[0] ? "# " : "* ");
    Serial.print(cd[1] == 0 ? "  " : down[1] ? "# " : "* ");
    Serial.print(cd[2] == 0 ? "  " : down[2] ? "# " : "* ");
    Serial.print(cd[3] == 0 ? "  " : down[3] ? "# " : "* ");
    Serial.print("|\t");
    Serial.print(threshold, 1);
    Serial.println();
    if(threshold <= 5){
      Serial.println();
      printing = false;
    }
  } 
#endif

  level[si] = new_level;
  si = key_next[si];

  long ddt = 300 - (micros() - t0);
  if(ddt > 3) delayMicroseconds(ddt);
  
}
