#include <stdint.h>
#include <math.h>
#include <TFTv2.h>
#include <SPI.h>

#define SCREEN_W 240
#define SCREEN_H 320
#define FIELD_TOP 35
#define FIELD_BOTTOM 312
#define FIELD_LEFT 8
#define FIELD_RIGHT 232
#define MAX_BACTERIA 12
#define START_BACTERIA 4
#define FOOD_COUNT 4
#define FRAME_DELAY 70
#define OLD_AGE 2500

struct Bacteria {
  float x, y, angle;
  int energy;
  long smellNow, smellPrev, smellOld;
  byte speed, sensor, turnGene, curiosity, persistence;
  unsigned int age;
  uint16_t color;
  bool alive;
};

struct Food {
  int x, y;
  bool exist;
  unsigned long respawnTime;
};

Bacteria bacteria[MAX_BACTERIA];
Food food[FOOD_COUNT];

unsigned long frameTimer = 0;
unsigned long frameCounter = 0;
bool simulationEnded = false;

int limitValue(int value, int minimum, int maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

uint16_t getBacteriaColor(byte speedGene) {
  if (speedGene == 1) return BLUE;
  if (speedGene == 2) return GREEN;
  return 0xF81F;
}

void createFood(byte index) {
  food[index].x = random(FIELD_LEFT + 10, FIELD_RIGHT - 10);
  food[index].y = random(FIELD_TOP + 10, FIELD_BOTTOM - 10);
  food[index].exist = true;
  food[index].respawnTime = 0;
}

void removeFood(byte index) {
  Tft.drawCircle(food[index].x, food[index].y, 5, BLACK);
  food[index].exist = false;
  food[index].respawnTime = millis() + random(1500, 4000);
}

long smellAt(float x, float y) {
  long bestSmell = -200000L;
  bool foodFound = false;

  for (byte i = 0; i < FOOD_COUNT; i++) {
    if (!food[i].exist) continue;

    long dx = food[i].x - (int)x;
    long dy = food[i].y - (int)y;
    long distanceSquared = dx * dx + dy * dy;
    long smell = 100000L - distanceSquared;

    if (!foodFound || smell > bestSmell) {
      bestSmell = smell;
      foodFound = true;
    }
  }

  return foodFound ? bestSmell : -200000L;
}

void initializeSmellMemory(byte index) {
  long smell = smellAt(bacteria[index].x, bacteria[index].y);
  bacteria[index].smellNow = smell;
  bacteria[index].smellPrev = smell;
  bacteria[index].smellOld = smell;
}

void createFounder(byte index) {
  Bacteria &b = bacteria[index];

  b.x = random(FIELD_LEFT + 15, FIELD_RIGHT - 15);
  b.y = random(FIELD_TOP + 15, FIELD_BOTTOM - 15);
  b.angle = random(0, 628) / 100.0;
  b.energy = 80;
  b.speed = random(1, 4);
  b.sensor = random(7, 18);
  b.turnGene = random(15, 51);
  b.curiosity = random(2, 13);
  b.persistence = random(15, 61);
  b.age = 0;
  b.color = getBacteriaColor(b.speed);
  b.alive = true;

  initializeSmellMemory(index);
}

void createChild(byte childIndex, byte parentIndex) {
  Bacteria &parent = bacteria[parentIndex];
  Bacteria &child = bacteria[childIndex];

  child.x = limitValue((int)(parent.x + random(-8, 9)), FIELD_LEFT + 5, FIELD_RIGHT - 5);
  child.y = limitValue((int)(parent.y + random(-8, 9)), FIELD_TOP + 5, FIELD_BOTTOM - 5);
  child.angle = parent.angle + random(-100, 101) / 100.0;
  child.speed = limitValue(parent.speed + random(-1, 2), 1, 3);
  child.sensor = limitValue(parent.sensor + random(-2, 3), 5, 22);
  child.turnGene = limitValue(parent.turnGene + random(-5, 6), 10, 70);
  child.curiosity = limitValue(parent.curiosity + random(-2, 3), 1, 25);
  child.persistence = limitValue(parent.persistence + random(-5, 6), 5, 90);
  child.energy = 45;
  child.age = 0;
  child.color = getBacteriaColor(child.speed);
  child.alive = true;

  initializeSmellMemory(childIndex);
}

int findFreeBacteriaSlot() {
  for (byte i = 0; i < MAX_BACTERIA; i++) {
    if (!bacteria[i].alive) return i;
  }
  return -1;
}

void updateSmellMemory(byte index) {
  Bacteria &b = bacteria[index];
  b.smellOld = b.smellPrev;
  b.smellPrev = b.smellNow;
  b.smellNow = smellAt(b.x, b.y);
}

void chemotaxis(byte index) {
  Bacteria &b = bacteria[index];

  float leftAngle = b.angle - 0.45;
  float rightAngle = b.angle + 0.45;
  long leftSmell = smellAt(b.x + cos(leftAngle) * b.sensor,
                           b.y + sin(leftAngle) * b.sensor);
  long rightSmell = smellAt(b.x + cos(rightAngle) * b.sensor,
                            b.y + sin(rightAngle) * b.sensor);

  float turnAmount = b.turnGene / 100.0;

  if (leftSmell > rightSmell) b.angle -= turnAmount;
  else if (rightSmell > leftSmell) b.angle += turnAmount;

  updateSmellMemory(index);

  bool smellIncreasing = b.smellNow >= b.smellPrev && b.smellPrev >= b.smellOld;
  bool smellDecreasing = b.smellNow < b.smellPrev && b.smellPrev < b.smellOld;

  long change = b.smellNow - b.smellPrev;
  if (change < 0) change = -change;
  bool smellStable = change < 20;

  if (smellDecreasing) {
    int tumbleProbability = limitValue(100 - b.persistence, 10, 85);
    if (random(0, 100) < tumbleProbability)
      b.angle += random(-180, 181) / 100.0;
  } else if (smellIncreasing) {
    if (random(0, 100) < 2)
      b.angle += random(-15, 16) / 100.0;
  } else if (smellStable) {
    if (random(0, 100) < b.curiosity)
      b.angle += random(-100, 101) / 100.0;
  } else {
    if (random(0, 200) < b.curiosity)
      b.angle += random(-40, 41) / 100.0;
  }
}

void moveBacteria(byte index) {
  Bacteria &b = bacteria[index];

  b.x += cos(b.angle) * b.speed;
  b.y += sin(b.angle) * b.speed;

  if (b.x < FIELD_LEFT) {
    b.x = FIELD_LEFT;
    b.angle = random(-100, 101) / 100.0;
  }
  if (b.x > FIELD_RIGHT) {
    b.x = FIELD_RIGHT;
    b.angle = 3.14 + random(-100, 101) / 100.0;
  }
  if (b.y < FIELD_TOP) {
    b.y = FIELD_TOP;
    b.angle = 1.57 + random(-100, 101) / 100.0;
  }
  if (b.y > FIELD_BOTTOM) {
    b.y = FIELD_BOTTOM;
    b.angle = 4.71 + random(-100, 101) / 100.0;
  }
}

void checkFood(byte bacteriaIndex) {
  Bacteria &b = bacteria[bacteriaIndex];

  for (byte i = 0; i < FOOD_COUNT; i++) {
    if (!food[i].exist) continue;

    long dx = food[i].x - (int)b.x;
    long dy = food[i].y - (int)b.y;

    if ((dx * dx + dy * dy) < 100) {
      b.energy += 45;
      if (b.energy > 140) b.energy = 140;
      removeFood(i);
    }
  }
}

void updateAgeAndEnergy(byte index) {
  Bacteria &b = bacteria[index];
  b.age++;

  if ((frameCounter % 4) == 0) b.energy--;
  if (b.speed == 3 && random(0, 6) == 0) b.energy--;
  if (b.sensor >= 18 && random(0, 12) == 0) b.energy--;
  if (b.age > OLD_AGE && random(0, 8) == 0) b.energy--;
  if (b.age > OLD_AGE + 1500 && random(0, 5) == 0) b.energy--;
}

void reproduce(byte parentIndex) {
  Bacteria &parent = bacteria[parentIndex];

  if (parent.age < 150) return;
  if (parent.age > OLD_AGE + 1000) return;
  if (parent.energy < 115) return;

  int freeSlot = findFreeBacteriaSlot();
  if (freeSlot < 0) return;

  parent.energy -= 55;
  createChild(freeSlot, parentIndex);
}

void checkDeath(byte index) {
  Bacteria &b = bacteria[index];

  if (b.energy > 0) return;

  Tft.drawCircle((int)b.x, (int)b.y, 4, BLACK);
  b.alive = false;
}

byte countAlive() {
  byte count = 0;
  for (byte i = 0; i < MAX_BACTERIA; i++) {
    if (bacteria[i].alive) count++;
  }
  return count;
}

void updateFood() {
  unsigned long currentTime = millis();

  for (byte i = 0; i < FOOD_COUNT; i++) {
    if (!food[i].exist && currentTime >= food[i].respawnTime)
      createFood(i);
  }
}

void drawAllFood() {
  for (byte i = 0; i < FOOD_COUNT; i++) {
    if (food[i].exist)
      Tft.drawCircle(food[i].x, food[i].y, 5, YELLOW);
  }
}

void setup() {
  randomSeed(analogRead(A5));

  for (byte i = 0; i < MAX_BACTERIA; i++)
    bacteria[i].alive = false;

  Tft.TFTinit();
  Tft.fillScreen();
  Tft.drawString("Bacteria Lab v1.1", 18, 8, 2, GREEN);

  for (byte i = 0; i < FOOD_COUNT; i++)
    createFood(i);

  for (byte i = 0; i < START_BACTERIA; i++)
    createFounder(i);

  drawAllFood();
}

void loop() {
  if (simulationEnded) return;
  if (millis() - frameTimer < FRAME_DELAY) return;

  frameTimer = millis();
  frameCounter++;

  for (byte i = 0; i < MAX_BACTERIA; i++) {
    if (bacteria[i].alive)
      Tft.drawCircle((int)bacteria[i].x, (int)bacteria[i].y, 4, BLACK);
  }

  updateFood();

  for (byte i = 0; i < MAX_BACTERIA; i++) {
    if (!bacteria[i].alive) continue;

    chemotaxis(i);
    moveBacteria(i);
    updateAgeAndEnergy(i);
    checkFood(i);
    reproduce(i);
    checkDeath(i);
  }

  drawAllFood();

  for (byte i = 0; i < MAX_BACTERIA; i++) {
    if (bacteria[i].alive)
      Tft.drawCircle((int)bacteria[i].x, (int)bacteria[i].y, 4, bacteria[i].color);
  }

  if (countAlive() == 0) {
    Tft.drawString("END", 85, 145, 4, RED);
    simulationEnded = true;
  }
}
