#include <stdint.h>
#include <math.h>
#include <TFTv2.h>
#include <SeeedTouchScreen.h>
#include <SPI.h>

// =====================================================
// Bacteria Lab v1.5 SELECT
// Arduino Uno + Seeed Studio 2.8" TFT Touch Shield v2.0
//
// Новое:
// - статистика в верхней части экрана;
// - Alive: число живых бактерий;
// - Born: общее число родившихся;
// - Dead: общее число умерших;
// - Food: число доступных источников пищи;
// - средние значения возраста и генов популяции;
// - сохранён круговорот вещества из v1.2;
// - сенсорные кнопки START, PAUSE, SELECT и STOP;
// - выбор бактерии касанием и просмотр её параметров.
// =====================================================

#define FIELD_TOP    35
#define FIELD_BOTTOM 278
#define FIELD_LEFT   8
#define FIELD_RIGHT  232

#define MAX_BACTERIA   12
#define START_BACTERIA 4
#define FOOD_COUNT     4

#define FRAME_DELAY 70
#define OLD_AGE 2500

#define BUTTON_TOP    286
#define BUTTON_BOTTOM 318

#define START_LEFT    4
#define START_RIGHT   58

#define PAUSE_LEFT    62
#define PAUSE_RIGHT   116

#define SELECT_LEFT   120
#define SELECT_RIGHT  174

#define STOP_LEFT     178
#define STOP_RIGHT    236

// Сенсор Seeed Studio 2.8" TFT Touch Shield v2.0
#define YP A2
#define XM A1
#define YM 14
#define XP 17

#define TS_MINX (116 * 2)
#define TS_MAXX (890 * 2)
#define TS_MINY (83 * 2)
#define TS_MAXY (913 * 2)

TouchScreen ts = TouchScreen(XP, YP, XM, YM);

enum SimulationState
{
  STATE_RUNNING,
  STATE_PAUSED,
  STATE_STOPPED
};

SimulationState simulationState = STATE_RUNNING;

struct Bacteria
{
  float x;
  float y;
  float angle;

  int energy;

  long smellNow;
  long smellPrev;
  long smellOld;

  byte speed;
  byte sensor;
  byte turnGene;
  byte curiosity;
  byte persistence;

  unsigned int age;

  uint16_t color;
  bool alive;
};

struct Food
{
  int x;
  int y;

  bool exist;

  unsigned long respawnTime;
};

Bacteria bacteria[MAX_BACTERIA];
Food food[FOOD_COUNT];

unsigned long frameTimer = 0;
unsigned long frameCounter = 0;

unsigned long totalBorn = 0;
unsigned long totalDead = 0;
unsigned long statisticsTimer = 0;
unsigned long lastTouchTime = 0;

char oldStatusLine1[40] = "";
char oldStatusLine2[40] = "";

bool simulationEnded = false;
bool selectMode = false;
bool infoScreenVisible = false;


// =====================================================
// Ограничение значения
// =====================================================

int limitValue(int value, int minimum, int maximum)
{
  if (value < minimum)
  {
    return minimum;
  }

  if (value > maximum)
  {
    return maximum;
  }

  return value;
}


// =====================================================
// Цвет бактерии определяется скоростью
// =====================================================

uint16_t getBacteriaColor(byte speedGene)
{
  if (speedGene == 1)
  {
    return BLUE;
  }

  if (speedGene == 2)
  {
    return GREEN;
  }

  return 0xF81F;
}


// =====================================================
// Случайное создание пищи
// =====================================================

void createFood(byte index)
{
  food[index].x =
    random(FIELD_LEFT + 10, FIELD_RIGHT - 10);

  food[index].y =
    random(FIELD_TOP + 10, FIELD_BOTTOM - 10);

  food[index].exist = true;
  food[index].respawnTime = 0;
}


// =====================================================
// Создание пищи в заданной точке
// Используется после смерти бактерии
// =====================================================

void createFoodAt(int x, int y)
{
  int slot = -1;

  // Сначала ищем свободную ячейку пищи
  for (byte i = 0; i < FOOD_COUNT; i++)
  {
    if (!food[i].exist)
    {
      slot = i;
      break;
    }
  }

  // Если свободной ячейки нет,
  // заменяем случайный источник пищи
  if (slot < 0)
  {
    slot = random(0, FOOD_COUNT);

    Tft.drawCircle(
      food[slot].x,
      food[slot].y,
      5,
      BLACK
    );
  }

  food[slot].x =
    limitValue(
      x,
      FIELD_LEFT + 6,
      FIELD_RIGHT - 6
    );

  food[slot].y =
    limitValue(
      y,
      FIELD_TOP + 6,
      FIELD_BOTTOM - 6
    );

  food[slot].exist = true;
  food[slot].respawnTime = 0;

  Tft.drawCircle(
    food[slot].x,
    food[slot].y,
    5,
    YELLOW
  );
}


// =====================================================
// Удаление съеденной пищи
// =====================================================

void removeFood(byte index)
{
  Tft.drawCircle(
    food[index].x,
    food[index].y,
    5,
    BLACK
  );

  food[index].exist = false;

  // Обычная пища появится снова через 1,5–4 секунды
  food[index].respawnTime =
    millis() + random(1500, 4000);
}


// =====================================================
// Сила запаха пищи
// =====================================================

long smellAt(float x, float y)
{
  long bestSmell = -200000L;
  bool foodFound = false;

  for (byte i = 0; i < FOOD_COUNT; i++)
  {
    if (!food[i].exist)
    {
      continue;
    }

    long dx =
      food[i].x - (int)x;

    long dy =
      food[i].y - (int)y;

    long distanceSquared =
      dx * dx + dy * dy;

    long smell =
      100000L - distanceSquared;

    if (!foodFound || smell > bestSmell)
    {
      bestSmell = smell;
      foodFound = true;
    }
  }

  if (!foodFound)
  {
    return -200000L;
  }

  return bestSmell;
}


// =====================================================
// Инициализация памяти запаха
// =====================================================

void initializeSmellMemory(byte index)
{
  long smell =
    smellAt(
      bacteria[index].x,
      bacteria[index].y
    );

  bacteria[index].smellNow = smell;
  bacteria[index].smellPrev = smell;
  bacteria[index].smellOld = smell;
}


// =====================================================
// Создание начальной бактерии
// =====================================================

void createFounder(byte index)
{
  Bacteria &b = bacteria[index];

  b.x =
    random(FIELD_LEFT + 15, FIELD_RIGHT - 15);

  b.y =
    random(FIELD_TOP + 15, FIELD_BOTTOM - 15);

  b.angle =
    random(0, 628) / 100.0;

  b.energy = 80;

  b.speed =
    random(1, 4);

  b.sensor =
    random(7, 18);

  b.turnGene =
    random(15, 51);

  b.curiosity =
    random(2, 13);

  b.persistence =
    random(15, 61);

  b.age = 0;

  b.color =
    getBacteriaColor(b.speed);

  b.alive = true;

  initializeSmellMemory(index);
}


// =====================================================
// Создание потомка
// =====================================================

void createChild(byte childIndex, byte parentIndex)
{
  Bacteria &parent = bacteria[parentIndex];
  Bacteria &child = bacteria[childIndex];

  child.x =
    parent.x + random(-8, 9);

  child.y =
    parent.y + random(-8, 9);

  child.x =
    limitValue(
      (int)child.x,
      FIELD_LEFT + 5,
      FIELD_RIGHT - 5
    );

  child.y =
    limitValue(
      (int)child.y,
      FIELD_TOP + 5,
      FIELD_BOTTOM - 5
    );

  child.angle =
    parent.angle +
    random(-100, 101) / 100.0;

  child.speed =
    limitValue(
      parent.speed + random(-1, 2),
      1,
      3
    );

  child.sensor =
    limitValue(
      parent.sensor + random(-2, 3),
      5,
      22
    );

  child.turnGene =
    limitValue(
      parent.turnGene + random(-5, 6),
      10,
      70
    );

  child.curiosity =
    limitValue(
      parent.curiosity + random(-2, 3),
      1,
      25
    );

  child.persistence =
    limitValue(
      parent.persistence + random(-5, 6),
      5,
      90
    );

  child.energy = 45;
  child.age = 0;

  child.color =
    getBacteriaColor(child.speed);

  child.alive = true;
  totalBorn++;

  initializeSmellMemory(childIndex);
}


// =====================================================
// Поиск свободного места для бактерии
// =====================================================

int findFreeBacteriaSlot()
{
  for (byte i = 0; i < MAX_BACTERIA; i++)
  {
    if (!bacteria[i].alive)
    {
      return i;
    }
  }

  return -1;
}


// =====================================================
// Обновление памяти запаха
// =====================================================

void updateSmellMemory(byte index)
{
  Bacteria &b = bacteria[index];

  b.smellOld = b.smellPrev;
  b.smellPrev = b.smellNow;
  b.smellNow = smellAt(b.x, b.y);
}


// =====================================================
// Хемотаксис
// =====================================================

void chemotaxis(byte index)
{
  Bacteria &b = bacteria[index];

  float leftAngle =
    b.angle - 0.45;

  float rightAngle =
    b.angle + 0.45;

  float leftX =
    b.x + cos(leftAngle) * b.sensor;

  float leftY =
    b.y + sin(leftAngle) * b.sensor;

  float rightX =
    b.x + cos(rightAngle) * b.sensor;

  float rightY =
    b.y + sin(rightAngle) * b.sensor;

  long leftSmell =
    smellAt(leftX, leftY);

  long rightSmell =
    smellAt(rightX, rightY);

  float turnAmount =
    b.turnGene / 100.0;

  if (leftSmell > rightSmell)
  {
    b.angle -= turnAmount;
  }
  else if (rightSmell > leftSmell)
  {
    b.angle += turnAmount;
  }

  updateSmellMemory(index);

  bool smellIncreasing =
    b.smellNow >= b.smellPrev &&
    b.smellPrev >= b.smellOld;

  bool smellDecreasing =
    b.smellNow < b.smellPrev &&
    b.smellPrev < b.smellOld;

  long change =
    b.smellNow - b.smellPrev;

  if (change < 0)
  {
    change = -change;
  }

  bool smellStable =
    change < 20;

  if (smellDecreasing)
  {
    int tumbleProbability =
      100 - b.persistence;

    tumbleProbability =
      limitValue(
        tumbleProbability,
        10,
        85
      );

    if (random(0, 100) < tumbleProbability)
    {
      b.angle +=
        random(-180, 181) / 100.0;
    }
  }
  else if (smellIncreasing)
  {
    if (random(0, 100) < 2)
    {
      b.angle +=
        random(-15, 16) / 100.0;
    }
  }
  else if (smellStable)
  {
    if (random(0, 100) < b.curiosity)
    {
      b.angle +=
        random(-100, 101) / 100.0;
    }
  }
  else
  {
    if (random(0, 200) < b.curiosity)
    {
      b.angle +=
        random(-40, 41) / 100.0;
    }
  }
}


// =====================================================
// Движение
// =====================================================

void moveBacteria(byte index)
{
  Bacteria &b = bacteria[index];

  b.x +=
    cos(b.angle) * b.speed;

  b.y +=
    sin(b.angle) * b.speed;

  if (b.x < FIELD_LEFT)
  {
    b.x = FIELD_LEFT;

    b.angle =
      random(-100, 101) / 100.0;
  }

  if (b.x > FIELD_RIGHT)
  {
    b.x = FIELD_RIGHT;

    b.angle =
      3.14 +
      random(-100, 101) / 100.0;
  }

  if (b.y < FIELD_TOP)
  {
    b.y = FIELD_TOP;

    b.angle =
      1.57 +
      random(-100, 101) / 100.0;
  }

  if (b.y > FIELD_BOTTOM)
  {
    b.y = FIELD_BOTTOM;

    b.angle =
      4.71 +
      random(-100, 101) / 100.0;
  }
}


// =====================================================
// Поедание пищи
// =====================================================

void checkFood(byte bacteriaIndex)
{
  Bacteria &b = bacteria[bacteriaIndex];

  for (byte i = 0; i < FOOD_COUNT; i++)
  {
    if (!food[i].exist)
    {
      continue;
    }

    long dx =
      food[i].x - (int)b.x;

    long dy =
      food[i].y - (int)b.y;

    if ((dx * dx + dy * dy) < 100)
    {
      b.energy += 45;

      if (b.energy > 140)
      {
        b.energy = 140;
      }

      removeFood(i);
    }
  }
}


// =====================================================
// Возраст и расход энергии
// =====================================================

void updateAgeAndEnergy(byte index)
{
  Bacteria &b = bacteria[index];

  b.age++;

  if ((frameCounter % 4) == 0)
  {
    b.energy--;
  }

  if (b.speed == 3)
  {
    if (random(0, 6) == 0)
    {
      b.energy--;
    }
  }

  if (b.sensor >= 18)
  {
    if (random(0, 12) == 0)
    {
      b.energy--;
    }
  }

  if (b.age > OLD_AGE)
  {
    if (random(0, 8) == 0)
    {
      b.energy--;
    }
  }

  if (b.age > OLD_AGE + 1500)
  {
    if (random(0, 5) == 0)
    {
      b.energy--;
    }
  }
}


// =====================================================
// Размножение
// =====================================================

void reproduce(byte parentIndex)
{
  Bacteria &parent = bacteria[parentIndex];

  if (parent.age < 150)
  {
    return;
  }

  if (parent.age > OLD_AGE + 1000)
  {
    return;
  }

  if (parent.energy < 115)
  {
    return;
  }

  int freeSlot =
    findFreeBacteriaSlot();

  if (freeSlot < 0)
  {
    return;
  }

  parent.energy -= 55;

  createChild(
    freeSlot,
    parentIndex
  );
}


// =====================================================
// Смерть и превращение в пищу
// =====================================================

void checkDeath(byte index)
{
  Bacteria &b = bacteria[index];

  if (b.energy > 0)
  {
    return;
  }

  int deathX = (int)b.x;
  int deathY = (int)b.y;

  // Стираем изображение бактерии
  Tft.drawCircle(
    deathX,
    deathY,
    4,
    BLACK
  );

  b.alive = false;
  totalDead++;

  // Останки бактерии становятся пищей
  createFoodAt(
    deathX,
    deathY
  );
}


// =====================================================
// Подсчёт живых бактерий
// =====================================================

byte countAlive()
{
  byte count = 0;

  for (byte i = 0; i < MAX_BACTERIA; i++)
  {
    if (bacteria[i].alive)
    {
      count++;
    }
  }

  return count;
}



// =====================================================
// Подсчёт доступной пищи
// =====================================================

byte countFood()
{
  byte count = 0;

  for (byte i = 0; i < FOOD_COUNT; i++)
  {
    if (food[i].exist)
    {
      count++;
    }
  }

  return count;
}


// =====================================================
// Вывод статистики
// =====================================================

void drawStatistics()
{
  byte alive = 0;

  unsigned long ageSum = 0;
  unsigned int speedSum = 0;
  unsigned int sensorSum = 0;
  unsigned int turnSum = 0;

  for (byte i = 0; i < MAX_BACTERIA; i++)
  {
    if (!bacteria[i].alive)
    {
      continue;
    }

    alive++;

    ageSum += bacteria[i].age;
    speedSum += bacteria[i].speed;
    sensorSum += bacteria[i].sensor;
    turnSum += bacteria[i].turnGene;
  }

  unsigned int averageAge = 0;
  byte averageSpeed = 0;
  byte averageSensor = 0;
  byte averageTurn = 0;

  if (alive > 0)
  {
    averageAge = ageSum / alive;
    averageSpeed = speedSum / alive;
    averageSensor = sensorSum / alive;
    averageTurn = turnSum / alive;
  }

  char statusLine1[40];
  char statusLine2[40];

  snprintf(
    statusLine1,
    sizeof(statusLine1),
    "A:%u B:%lu D:%lu F:%u",
    alive,
    totalBorn,
    totalDead,
    countFood()
  );

  snprintf(
    statusLine2,
    sizeof(statusLine2),
    "Age:%u Sp:%u Se:%u Tu:%u",
    averageAge,
    averageSpeed,
    averageSensor,
    averageTurn
  );

  Tft.drawString(oldStatusLine1, 2, 2, 1, BLACK);
  Tft.drawString(oldStatusLine2, 2, 18, 1, BLACK);

  Tft.drawString(statusLine1, 2, 2, 1, WHITE);
  Tft.drawString(statusLine2, 2, 18, 1, CYAN);

  strncpy(oldStatusLine1, statusLine1, sizeof(oldStatusLine1));
  oldStatusLine1[sizeof(oldStatusLine1) - 1] = '\0';

  strncpy(oldStatusLine2, statusLine2, sizeof(oldStatusLine2));
  oldStatusLine2[sizeof(oldStatusLine2) - 1] = '\0';
}


// =====================================================
// Возрождение обычной пищи
// =====================================================

void updateFood()
{
  unsigned long currentTime =
    millis();

  for (byte i = 0; i < FOOD_COUNT; i++)
  {
    if (
      !food[i].exist &&
      currentTime >= food[i].respawnTime
    )
    {
      createFood(i);
    }
  }
}


// =====================================================
// Отрисовка пищи
// =====================================================

void drawAllFood()
{
  for (byte i = 0; i < FOOD_COUNT; i++)
  {
    if (food[i].exist)
    {
      Tft.drawCircle(
        food[i].x,
        food[i].y,
        5,
        YELLOW
      );
    }
  }
}



// =====================================================
// Рисование нижних сенсорных кнопок
// =====================================================

void drawControlButtons()
{
  Tft.drawRectangle(
    START_LEFT,
    BUTTON_TOP,
    START_RIGHT - START_LEFT,
    BUTTON_BOTTOM - BUTTON_TOP,
    GREEN
  );

  Tft.drawRectangle(
    PAUSE_LEFT,
    BUTTON_TOP,
    PAUSE_RIGHT - PAUSE_LEFT,
    BUTTON_BOTTOM - BUTTON_TOP,
    YELLOW
  );

  Tft.drawRectangle(
    SELECT_LEFT,
    BUTTON_TOP,
    SELECT_RIGHT - SELECT_LEFT,
    BUTTON_BOTTOM - BUTTON_TOP,
    CYAN
  );

  Tft.drawRectangle(
    STOP_LEFT,
    BUTTON_TOP,
    STOP_RIGHT - STOP_LEFT,
    BUTTON_BOTTOM - BUTTON_TOP,
    RED
  );

  Tft.drawString("START",  START_LEFT + 6, BUTTON_TOP + 10, 1, GREEN);
  Tft.drawString("PAUSE",  PAUSE_LEFT + 6, BUTTON_TOP + 10, 1, YELLOW);
  Tft.drawString("SELECT", SELECT_LEFT + 4, BUTTON_TOP + 10, 1, CYAN);
  Tft.drawString("STOP",   STOP_LEFT + 11, BUTTON_TOP + 10, 1, RED);
}


// =====================================================
// Очистка только игровой области
// =====================================================

void clearField()
{
  // У библиотеки нет обычного fillRect, поэтому очищаем линиями.
  for (int y = FIELD_TOP; y <= FIELD_BOTTOM; y++)
  {
    Tft.drawHorizontalLine(
      FIELD_LEFT,
      y,
      FIELD_RIGHT - FIELD_LEFT,
      BLACK
    );
  }
}



// =====================================================
// Перерисовка текущего мира
// =====================================================

void redrawWorld()
{
  clearField();
  drawAllFood();

  for (byte i = 0; i < MAX_BACTERIA; i++)
  {
    if (bacteria[i].alive)
    {
      Tft.drawCircle(
        (int)bacteria[i].x,
        (int)bacteria[i].y,
        4,
        bacteria[i].color
      );
    }
  }
}


// =====================================================
// Поиск бактерии рядом с точкой касания
// =====================================================

int findBacteriaNear(int touchX, int touchY)
{
  int bestIndex = -1;
  long bestDistance = 400L;

  for (byte i = 0; i < MAX_BACTERIA; i++)
  {
    if (!bacteria[i].alive)
    {
      continue;
    }

    long dx = (long)((int)bacteria[i].x - touchX);
    long dy = (long)((int)bacteria[i].y - touchY);
    long distanceSquared = dx * dx + dy * dy;

    if (
      distanceSquared <= 400L &&
      distanceSquared < bestDistance
    )
    {
      bestDistance = distanceSquared;
      bestIndex = i;
    }
  }

  return bestIndex;
}


// =====================================================
// Экран параметров выбранной бактерии
// =====================================================

void showBacteriaInfo(byte index)
{
  Bacteria &b = bacteria[index];

  clearField();

  char line[28];

  snprintf(line, sizeof(line), "BACTERIA #%u", index);
  Tft.drawString(line, 48, 55, 2, WHITE);

  snprintf(line, sizeof(line), "Energy: %d", b.energy);
  Tft.drawString(line, 35, 90, 2, GREEN);

  snprintf(line, sizeof(line), "Age: %u", b.age);
  Tft.drawString(line, 35, 115, 2, CYAN);

  snprintf(line, sizeof(line), "Speed: %u", b.speed);
  Tft.drawString(line, 35, 140, 2, WHITE);

  snprintf(line, sizeof(line), "Sensor: %u", b.sensor);
  Tft.drawString(line, 35, 165, 2, WHITE);

  snprintf(line, sizeof(line), "Turn: %u", b.turnGene);
  Tft.drawString(line, 35, 190, 2, WHITE);

  snprintf(line, sizeof(line), "Curiosity: %u", b.curiosity);
  Tft.drawString(line, 35, 215, 2, WHITE);

  snprintf(line, sizeof(line), "Persist: %u", b.persistence);
  Tft.drawString(line, 35, 240, 2, WHITE);

  Tft.drawString("START = RETURN", 55, 266, 1, YELLOW);

  infoScreenVisible = true;
  selectMode = false;
  simulationState = STATE_PAUSED;
}


// =====================================================
// Новый запуск симуляции
// =====================================================

void restartSimulation()
{
  clearField();

  for (byte i = 0; i < MAX_BACTERIA; i++)
  {
    bacteria[i].alive = false;
  }

  for (byte i = 0; i < FOOD_COUNT; i++)
  {
    food[i].exist = false;
    food[i].respawnTime = 0;
  }

  frameCounter = 0;
  totalBorn = 0;
  totalDead = 0;
  simulationEnded = false;

  oldStatusLine1[0] = '\0';
  oldStatusLine2[0] = '\0';

  for (byte i = 0; i < FOOD_COUNT; i++)
  {
    createFood(i);
  }

  for (byte i = 0; i < START_BACTERIA; i++)
  {
    createFounder(i);
  }

  totalBorn = START_BACTERIA;

  drawAllFood();
  drawStatistics();
  drawControlButtons();

  simulationState = STATE_RUNNING;
  frameTimer = millis();
}


// =====================================================
// Остановка симуляции
// =====================================================

void stopSimulation()
{
  simulationState = STATE_STOPPED;
  simulationEnded = false;
  selectMode = false;
  infoScreenVisible = false;

  clearField();

  Tft.drawString(
    "STOPPED",
    70,
    145,
    3,
    RED
  );

  drawControlButtons();
}


// =====================================================
// Проверка сенсорных кнопок
// =====================================================

void handleTouch()
{
  Point p = ts.getPoint();

  if (p.z <= __PRESSURE)
  {
    return;
  }

  if (millis() - lastTouchTime < 300)
  {
    return;
  }

  lastTouchTime = millis();

  int touchX = map(
    p.x,
    TS_MINX,
    TS_MAXX,
    0,
    240
  );

  int touchY = map(
    p.y,
    TS_MINY,
    TS_MAXY,
    0,
    320
  );

  touchX = limitValue(touchX, 0, 239);
  touchY = limitValue(touchY, 0, 319);

  if (
    selectMode &&
    touchX >= FIELD_LEFT &&
    touchX <= FIELD_RIGHT &&
    touchY >= FIELD_TOP &&
    touchY <= FIELD_BOTTOM
  )
  {
    int selected = findBacteriaNear(touchX, touchY);

    if (selected >= 0)
    {
      showBacteriaInfo((byte)selected);
    }
    else
    {
      Tft.drawString("MISS - TRY AGAIN", 55, 266, 1, RED);
    }

    return;
  }

  if (
    touchY < BUTTON_TOP ||
    touchY > BUTTON_BOTTOM
  )
  {
    return;
  }

  if (
    touchX >= START_LEFT &&
    touchX <= START_RIGHT
  )
  {
    if (simulationState == STATE_STOPPED || simulationEnded)
    {
      restartSimulation();
    }
    else
    {
      if (infoScreenVisible || selectMode)
      {
        infoScreenVisible = false;
        selectMode = false;
        redrawWorld();
      }

      simulationState = STATE_RUNNING;
      frameTimer = millis();
    }

    return;
  }

  if (
    touchX >= PAUSE_LEFT &&
    touchX <= PAUSE_RIGHT
  )
  {
    if (simulationState == STATE_RUNNING)
    {
      simulationState = STATE_PAUSED;
    }

    return;
  }

  if (
    touchX >= SELECT_LEFT &&
    touchX <= SELECT_RIGHT
  )
  {
    if (
      simulationState != STATE_STOPPED &&
      !simulationEnded
    )
    {
      simulationState = STATE_PAUSED;
      selectMode = true;
      infoScreenVisible = false;

      redrawWorld();
      Tft.drawString("TOUCH BACTERIA", 58, 266, 1, CYAN);
    }

    return;
  }

  if (
    touchX >= STOP_LEFT &&
    touchX <= STOP_RIGHT
  )
  {
    stopSimulation();
  }
}


// =====================================================
// SETUP
// =====================================================

void setup()
{
  randomSeed(analogRead(A5));

  for (byte i = 0; i < MAX_BACTERIA; i++)
  {
    bacteria[i].alive = false;
  }

  Tft.TFTinit();
  Tft.fillScreen();

  for (byte i = 0; i < FOOD_COUNT; i++)
  {
    createFood(i);
  }

  for (byte i = 0; i < START_BACTERIA; i++)
  {
    createFounder(i);
  }

  totalBorn = START_BACTERIA;

  drawAllFood();
  drawStatistics();
  drawControlButtons();
}


// =====================================================
// LOOP
// =====================================================

void loop()
{
  handleTouch();

  if (
    simulationState == STATE_PAUSED ||
    simulationState == STATE_STOPPED ||
    simulationEnded
  )
  {
    return;
  }

  if (millis() - frameTimer < FRAME_DELAY)
  {
    return;
  }

  frameTimer = millis();
  frameCounter++;

  // Стираем старое положение бактерий
  for (byte i = 0; i < MAX_BACTERIA; i++)
  {
    if (bacteria[i].alive)
    {
      Tft.drawCircle(
        (int)bacteria[i].x,
        (int)bacteria[i].y,
        4,
        BLACK
      );
    }
  }

  updateFood();

  // Обновляем бактерии
  for (byte i = 0; i < MAX_BACTERIA; i++)
  {
    if (!bacteria[i].alive)
    {
      continue;
    }

    chemotaxis(i);
    moveBacteria(i);
    updateAgeAndEnergy(i);
    checkFood(i);
    reproduce(i);
    checkDeath(i);
  }

  drawAllFood();

  // Рисуем живые бактерии
  for (byte i = 0; i < MAX_BACTERIA; i++)
  {
    if (bacteria[i].alive)
    {
      Tft.drawCircle(
        (int)bacteria[i].x,
        (int)bacteria[i].y,
        4,
        bacteria[i].color
      );
    }
  }

  if (millis() - statisticsTimer >= 500)
  {
    statisticsTimer = millis();
    drawStatistics();
  }

  if (countAlive() == 0)
  {
    drawStatistics();

    Tft.drawString(
      "END",
      85,
      145,
      4,
      RED
    );

    simulationEnded = true;
    simulationState = STATE_STOPPED;
    drawControlButtons();
  }
}