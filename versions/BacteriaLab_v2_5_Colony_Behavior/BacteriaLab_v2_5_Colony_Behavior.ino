#include <stdint.h>
#include <math.h>
#include <TFTv2.h>
#include <SeeedTouchScreen.h>
#include <SPI.h>

// =====================================================
// Bacteria Lab v2.5 COLONY BEHAVIOR
// Arduino Uno + Seeed Studio 2.8" TFT Touch Shield v2.0
//
// Новое:
// - уникальный ID каждой бактерии;
// - ID родителя;
// - номер поколения;
// - счётчик потомков;
// - родословная выводится на экране SELECT;
// - четыре вида бактерий: Explorer, Sprinter, Survivor, Breeder;
// - вид наследуется, но иногда мутирует;
// - виды отличаются сенсорами, скоростью, старением и размножением;
// - бактерии чувствуют не только обычную пищу, но и остаточная биомасса;
// - при голоде клеточные остатки становятся более привлекательными;
// - визуальная отметка атаки убрана;
// - погибшая бактерия превращается в оранжевый маркер остаточной биомассы;
// - остаточная биомасса сохраняется на поле, пока его не съедят;
// - утилизация остаточная биомассаа восстанавливает энергию;
// - сохранены виды, родословная, хемотаксис и сенсорные кнопки.
// =====================================================

#define FIELD_TOP    20
#define FIELD_BOTTOM 278
#define FIELD_LEFT   8
#define FIELD_RIGHT  232

#define MAX_BACTERIA   16
#define START_BACTERIA 6
#define FOOD_COUNT     4
#define MAX_BIOMASS_OBJECTS     8

#define FRAME_DELAY 70
#define OLD_AGE 2500
#define BIOMASS_COLOR 0xFD20

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

enum Species
{
  SPECIES_EXPLORER = 0,
  SPECIES_SPRINTER = 1,
  SPECIES_SURVIVOR = 2,
  SPECIES_BREEDER = 3
};

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
  byte species;

  unsigned int age;

  // Родословная
  uint16_t id;
  uint16_t parentId;
  byte generation;
  byte children;

  byte attackCooldown;
  byte reproductionCooldown;
  byte kills;

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

struct Biomass
{
  int x;
  int y;
  byte nutrition;
  bool exist;
};

Bacteria bacteria[MAX_BACTERIA];
Food food[FOOD_COUNT];
Biomass biomassObjects[MAX_BIOMASS_OBJECTS];

unsigned long frameTimer = 0;
unsigned long frameCounter = 0;

unsigned long totalBorn = 0;
unsigned long totalDead = 0;
unsigned long statisticsTimer = 0;
unsigned long lastTouchTime = 0;

// Следующий уникальный номер бактерии.
// Значение 0 зарезервировано: у основателя нет родителя.
uint16_t nextBacteriaId = 1;

char oldStatusLine1[40] = "";

bool simulationEnded = false;
bool selectMode = false;
bool infoScreenVisible = false;
byte selectedBacteriaIndex = 0;
byte inspectorPage = 0;


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

uint16_t getBacteriaColor(byte species)
{
  if (species == SPECIES_EXPLORER)
  {
    return GREEN;
  }

  if (species == SPECIES_SPRINTER)
  {
    return BLUE;
  }

  if (species == SPECIES_SURVIVOR)
  {
    return 0xF81F; // фиолетовый
  }

  return RED; // Breeder
}


const char* getSpeciesName(byte species)
{
  if (species == SPECIES_EXPLORER)
  {
    return "Explorer";
  }

  if (species == SPECIES_SPRINTER)
  {
    return "Sprinter";
  }

  if (species == SPECIES_SURVIVOR)
  {
    return "Survivor";
  }

  return "Breeder";
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
  bool sourceFound = false;

  // Обычная пища
  for (byte i = 0; i < FOOD_COUNT; i++)
  {
    if (!food[i].exist)
    {
      continue;
    }

    long dx = food[i].x - (int)x;
    long dy = food[i].y - (int)y;
    long distanceSquared = dx * dx + dy * dy;

    long smell = 100000L - distanceSquared;

    if (!sourceFound || smell > bestSmell)
    {
      bestSmell = smell;
      sourceFound = true;
    }
  }

  // Остаточная биомасса тоже создают пищевой запах.
  // Запах немного сильнее, чтобы голодные бактерии
  // уверенно находили клеточные остатки.
  for (byte i = 0; i < MAX_BIOMASS_OBJECTS; i++)
  {
    if (!biomassObjects[i].exist)
    {
      continue;
    }

    long dx = biomassObjects[i].x - (int)x;
    long dy = biomassObjects[i].y - (int)y;
    long distanceSquared = dx * dx + dy * dy;

    long smell =
      108000L +
      ((long)biomassObjects[i].nutrition * 40L) -
      distanceSquared;

    if (!sourceFound || smell > bestSmell)
    {
      bestSmell = smell;
      sourceFound = true;
    }
  }

  if (!sourceFound)
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
  b.species = random(0, 4);

  // Базовые особенности видов
  if (b.species == SPECIES_EXPLORER)
  {
    b.speed = 2;
    b.sensor = random(16, 23);
    b.turnGene = random(25, 51);
    b.curiosity = random(12, 26);
    b.persistence = random(20, 61);
  }
  else if (b.species == SPECIES_SPRINTER)
  {
    b.speed = 3;
    b.sensor = random(8, 16);
    b.turnGene = random(20, 56);
    b.curiosity = random(5, 16);
    b.persistence = random(20, 61);
  }
  else if (b.species == SPECIES_SURVIVOR)
  {
    b.speed = 1;
    b.sensor = random(10, 19);
    b.turnGene = random(15, 46);
    b.curiosity = random(3, 11);
    b.persistence = random(45, 81);
  }
  else
  {
    b.speed = 2;
    b.sensor = random(7, 15);
    b.turnGene = random(20, 51);
    b.curiosity = random(5, 14);
    b.persistence = random(15, 51);
  }

  b.age = 0;

  // Основатель новой линии
  b.id = nextBacteriaId++;
  if (nextBacteriaId == 0)
  {
    nextBacteriaId = 1;
  }

  b.parentId = 0;
  b.generation = 0;
  b.children = 0;
  b.attackCooldown = 0;
  b.reproductionCooldown = 0;
  b.kills = 0;

  b.color =
    getBacteriaColor(b.species);

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

  int spawnDistance = random(12, 19);
  float spawnAngle =
    parent.angle +
    random(-314, 315) / 100.0;

  child.x =
    parent.x +
    cos(spawnAngle) * spawnDistance;

  child.y =
    parent.y +
    sin(spawnAngle) * spawnDistance;

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

  // Обычно вид наследуется. В 5% случаев происходит смена вида.
  child.species = parent.species;

  if (random(0, 100) < 5)
  {
    child.species = random(0, 4);
  }

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

  // Вид немного подталкивает потомка к характерным признакам.
  if (child.species == SPECIES_EXPLORER)
  {
    child.sensor = limitValue(child.sensor + 2, 5, 22);
    child.curiosity = limitValue(child.curiosity + 2, 1, 25);
  }
  else if (child.species == SPECIES_SPRINTER)
  {
    child.speed = 3;
  }
  else if (child.species == SPECIES_SURVIVOR)
  {
    child.speed = limitValue(child.speed - 1, 1, 3);
    child.persistence = limitValue(child.persistence + 5, 5, 90);
  }

  child.energy =
    (child.species == SPECIES_BREEDER) ? 40 : 45;
  child.age = 0;

  // Родословная потомка
  child.id = nextBacteriaId++;
  if (nextBacteriaId == 0)
  {
    nextBacteriaId = 1;
  }

  child.parentId = parent.id;

  if (parent.generation < 255)
  {
    child.generation = parent.generation + 1;
  }
  else
  {
    child.generation = 255;
  }

  child.children = 0;
  child.attackCooldown = 0;
  child.reproductionCooldown = 35;
  child.kills = 0;

  if (parent.children < 255)
  {
    parent.children++;
  }

  child.color =
    getBacteriaColor(child.species);

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

  if (b.energy < 40)
  {
    long leftPrey =
      preySmellAt(index, leftX, leftY);

    long rightPrey =
      preySmellAt(index, rightX, rightY);

    if (leftPrey > leftSmell)
    {
      leftSmell = leftPrey;
    }

    if (rightPrey > rightSmell)
    {
      rightSmell = rightPrey;
    }
  }

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
// Поведение колонии
// =====================================================

void colonyBehavior(byte index)
{
  Bacteria &b = bacteria[index];

  long nearestDistance = 100000L;
  int nearestIndex = -1;

  long relativeX = 0;
  long relativeY = 0;
  byte relativeCount = 0;

  for (byte i = 0; i < MAX_BACTERIA; i++)
  {
    if (
      i == index ||
      !bacteria[i].alive
    )
    {
      continue;
    }

    long dx =
      (long)((int)bacteria[i].x - (int)b.x);

    long dy =
      (long)((int)bacteria[i].y - (int)b.y);

    long distanceSquared =
      dx * dx + dy * dy;

    if (distanceSquared < nearestDistance)
    {
      nearestDistance = distanceSquared;
      nearestIndex = i;
    }

    // Родственники образуют рыхлую колонию.
    if (
      isCloseRelative(index, i) &&
      distanceSquared < 2500L
    )
    {
      relativeX += (int)bacteria[i].x;
      relativeY += (int)bacteria[i].y;
      relativeCount++;
    }
  }

  // Сильное разведение при тесном контакте.
  if (
    nearestIndex >= 0 &&
    nearestDistance < 100L
  )
  {
    float awayX =
      b.x - bacteria[nearestIndex].x;

    float awayY =
      b.y - bacteria[nearestIndex].y;

    float headingX = cos(b.angle);
    float headingY = sin(b.angle);

    float cross =
      headingX * awayY -
      headingY * awayX;

    if (cross >= 0)
    {
      b.angle += 0.65;
    }
    else
    {
      b.angle -= 0.65;
    }

    return;
  }

  // При нормальном запасе энергии слегка держимся возле семьи.
  // Голодная бактерия продолжает искать пищу или добычу.
  if (
    relativeCount > 0 &&
    b.energy >= 40
  )
  {
    float centerX =
      (float)relativeX / relativeCount;

    float centerY =
      (float)relativeY / relativeCount;

    float toCenterX = centerX - b.x;
    float toCenterY = centerY - b.y;

    long centerDistance =
      (long)(toCenterX * toCenterX +
             toCenterY * toCenterY);

    // Не сбиваемся в плотный комок:
    // притяжение действует только с некоторого расстояния.
    if (centerDistance > 400L)
    {
      float headingX = cos(b.angle);
      float headingY = sin(b.angle);

      float cross =
        headingX * toCenterY -
        headingY * toCenterX;

      if (cross >= 0)
      {
        b.angle += 0.10;
      }
      else
      {
        b.angle -= 0.10;
      }
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
// Проверка близкого родства
// =====================================================

bool isOwnChild(byte attackerIndex, byte targetIndex)
{
  return bacteria[targetIndex].parentId ==
         bacteria[attackerIndex].id;
}

bool isCloseRelative(byte attackerIndex, byte targetIndex)
{
  Bacteria &attacker = bacteria[attackerIndex];
  Bacteria &target = bacteria[targetIndex];

  if (attacker.parentId == target.id)
  {
    return true;
  }

  if (target.parentId == attacker.id)
  {
    return true;
  }

  if (
    attacker.parentId != 0 &&
    attacker.parentId == target.parentId
  )
  {
    return true;
  }

  return false;
}

bool canAttackTarget(byte attackerIndex, byte targetIndex)
{
  if (
    attackerIndex == targetIndex ||
    !bacteria[targetIndex].alive
  )
  {
    return false;
  }

  if (isOwnChild(attackerIndex, targetIndex))
  {
    return false;
  }

  if (bacteria[attackerIndex].energy <= 20)
  {
    return true;
  }

  return !isCloseRelative(attackerIndex, targetIndex);
}

long preySmellAt(byte attackerIndex, float x, float y)
{
  long bestSmell = -200000L;
  bool preyFound = false;

  for (byte i = 0; i < MAX_BACTERIA; i++)
  {
    if (!canAttackTarget(attackerIndex, i))
    {
      continue;
    }

    long dx = (long)((int)bacteria[i].x - (int)x);
    long dy = (long)((int)bacteria[i].y - (int)y);
    long distanceSquared = dx * dx + dy * dy;

    long speciesBonus =
      (bacteria[i].species != bacteria[attackerIndex].species)
      ? 5000L
      : 0L;

    long smell = 104000L + speciesBonus - distanceSquared;

    if (!preyFound || smell > bestSmell)
    {
      bestSmell = smell;
      preyFound = true;
    }
  }

  return preyFound ? bestSmell : -200000L;
}


// =====================================================
// Атака соседней бактерии
// =====================================================

void checkAttack(byte attackerIndex)
{
  Bacteria &attacker = bacteria[attackerIndex];

  if (!attacker.alive)
  {
    return;
  }

  if (attacker.attackCooldown > 0)
  {
    attacker.attackCooldown--;
    return;
  }

  // Атакуем только при заметной нехватке энергии.
  if (attacker.energy >= 40)
  {
    return;
  }

  int nearestIndex = -1;
  long nearestDistance = 65L;

  for (byte i = 0; i < MAX_BACTERIA; i++)
  {
    if (!canAttackTarget(attackerIndex, i))
    {
      continue;
    }

    long dx =
      (long)((int)bacteria[i].x - (int)attacker.x);

    long dy =
      (long)((int)bacteria[i].y - (int)attacker.y);

    long distanceSquared =
      dx * dx + dy * dy;

    if (
      distanceSquared < nearestDistance
    )
    {
      nearestDistance = distanceSquared;
      nearestIndex = i;
    }
  }

  if (nearestIndex < 0)
  {
    return;
  }

  Bacteria &victim = bacteria[nearestIndex];

  const byte stolenEnergy = 12;

  victim.energy -= stolenEnergy;
  attacker.energy += 8;

  if (attacker.energy > 140)
  {
    attacker.energy = 140;
  }

  attacker.attackCooldown = 18;

  if (victim.energy <= 0)
  {
    if (attacker.kills < 255)
    {
      attacker.kills++;
    }
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
      b.energy += 55;

      if (b.energy > 150)
      {
        b.energy = 150;
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

  if (b.reproductionCooldown > 0)
  {
    b.reproductionCooldown--;
  }

  if ((frameCounter % 5) == 0)
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

  unsigned int speciesOldAge = OLD_AGE;

  if (b.species == SPECIES_SURVIVOR)
  {
    speciesOldAge = OLD_AGE + 1500;
  }

  if (b.age > speciesOldAge)
  {
    if (random(0, 8) == 0)
    {
      b.energy--;
    }
  }

  if (b.age > speciesOldAge + 1500)
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

  if (parent.reproductionCooldown > 0)
  {
    return;
  }

  if (parent.age < 150)
  {
    return;
  }

  unsigned int maxReproductiveAge = OLD_AGE + 1000;

  if (parent.species == SPECIES_SURVIVOR)
  {
    maxReproductiveAge += 1200;
  }

  if (parent.age > maxReproductiveAge)
  {
    return;
  }

  int reproductionThreshold = 100;
  int reproductionCost = 45;

  if (parent.species == SPECIES_BREEDER)
  {
    reproductionThreshold = 96;
    reproductionCost = 46;
  }

  if (parent.species == SPECIES_EXPLORER)
  {
    reproductionThreshold = 108;
  }

  if (parent.energy < reproductionThreshold)
  {
    return;
  }

  int freeSlot =
    findFreeBacteriaSlot();

  if (freeSlot < 0)
  {
    return;
  }

  parent.energy -= reproductionCost;

  createChild(
    freeSlot,
    parentIndex
  );

  if (parent.species == SPECIES_BREEDER)
  {
    parent.reproductionCooldown = 55;
  }
  else
  {
    parent.reproductionCooldown = 90;
  }
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

  // Клеточные остатки остаются на поле до утилизации.
  createBiomass(
    deathX,
    deathY,
    38
  );
}


// =====================================================
// Создание остаточная биомассаа
// =====================================================

void createBiomass(int x, int y, byte nutrition)
{
  int slot = -1;

  for (byte i = 0; i < MAX_BIOMASS_OBJECTS; i++)
  {
    if (!biomassObjects[i].exist)
    {
      slot = i;
      break;
    }
  }

  // Если мест больше нет, заменяем самый первый остаточная биомасса.
  if (slot < 0)
  {
    slot = 0;

    Tft.drawRectangle(
      biomassObjects[slot].x - 3,
      biomassObjects[slot].y - 3,
      6,
      6,
      BLACK
    );
  }

  biomassObjects[slot].x = limitValue(x, FIELD_LEFT + 4, FIELD_RIGHT - 4);
  biomassObjects[slot].y = limitValue(y, FIELD_TOP + 4, FIELD_BOTTOM - 4);
  biomassObjects[slot].nutrition = nutrition;
  biomassObjects[slot].exist = true;
}


// =====================================================
// Отрисовка объектов остаточной биомассы
// =====================================================

void drawAllBiomasss()
{
  for (byte i = 0; i < MAX_BIOMASS_OBJECTS; i++)
  {
    if (!biomassObjects[i].exist)
    {
      continue;
    }

    Tft.drawRectangle(
      biomassObjects[i].x - 3,
      biomassObjects[i].y - 3,
      6,
      6,
      BIOMASS_COLOR
    );
  }
}


// =====================================================
// Поедание остаточная биомассаа
// =====================================================

void checkBiomass(byte bacteriaIndex)
{
  Bacteria &b = bacteria[bacteriaIndex];

  for (byte i = 0; i < MAX_BIOMASS_OBJECTS; i++)
  {
    if (!biomassObjects[i].exist)
    {
      continue;
    }

    long dx = biomassObjects[i].x - (int)b.x;
    long dy = biomassObjects[i].y - (int)b.y;

    if ((dx * dx + dy * dy) < 100)
    {
      b.energy += biomassObjects[i].nutrition;

      if (b.energy > 150)
      {
        b.energy = 150;
      }

      Tft.drawRectangle(
        biomassObjects[i].x - 3,
        biomassObjects[i].y - 3,
        6,
        6,
        BLACK
      );

      biomassObjects[i].exist = false;
      return;
    }
  }
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
  char statusLine[40];

  snprintf(
    statusLine,
    sizeof(statusLine),
    "A:%u B:%lu D:%lu F:%u",
    countAlive(),
    totalBorn,
    totalDead,
    countFood()
  );

  // Стираем предыдущую строку.
  Tft.drawString(
    oldStatusLine1,
    2,
    2,
    1,
    BLACK
  );

  // Рисуем только краткую статистику.
  Tft.drawString(
    statusLine,
    2,
    2,
    1,
    WHITE
  );

  strncpy(
    oldStatusLine1,
    statusLine,
    sizeof(oldStatusLine1)
  );

  oldStatusLine1[
    sizeof(oldStatusLine1) - 1
  ] = '\0';
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
  drawAllBiomasss();

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

  char line[30];

  snprintf(line, sizeof(line), "BACTERIA #%u", b.id);
  Tft.drawString(line, 45, 48, 2, WHITE);

  if (inspectorPage == 0)
  {
    snprintf(line, sizeof(line), "Energy:%d", b.energy);
    Tft.drawString(line, 25, 80, 2, GREEN);

    snprintf(line, sizeof(line), "Age:%u", b.age);
    Tft.drawString(line, 25, 105, 2, CYAN);

    snprintf(line, sizeof(line), "Type:%s", getSpeciesName(b.species));
    Tft.drawString(line, 25, 130, 2, b.color);

    snprintf(line, sizeof(line), "Parent:%u", b.parentId);
    Tft.drawString(line, 25, 155, 2, WHITE);

    snprintf(
      line,
      sizeof(line),
      "Gen:%u Child:%u",
      b.generation,
      b.children
    );
    Tft.drawString(line, 25, 180, 2, WHITE);

    snprintf(
      line,
      sizeof(line),
      "Speed:%u Sensor:%u",
      b.speed,
      b.sensor
    );
    Tft.drawString(line, 25, 205, 2, WHITE);

    snprintf(line, sizeof(line), "Turn:%u", b.turnGene);
    Tft.drawString(line, 25, 230, 2, WHITE);

    Tft.drawString("SELECT = MORE", 60, 258, 1, YELLOW);
  }
  else
  {
    Tft.drawString("BEHAVIOUR GENES", 38, 82, 2, CYAN);

    snprintf(line, sizeof(line), "Curiosity:%u", b.curiosity);
    Tft.drawString(line, 30, 125, 2, WHITE);

    snprintf(line, sizeof(line), "Persistence:%u", b.persistence);
    Tft.drawString(line, 30, 150, 2, WHITE);

    snprintf(
      line,
      sizeof(line),
      "Kills:%u Cool:%u",
      b.kills,
      b.reproductionCooldown
    );
    Tft.drawString(line, 30, 180, 2, RED);

    snprintf(line, sizeof(line), "Speed:%u Sensor:%u", b.speed, b.sensor);
    Tft.drawString(line, 30, 210, 2, WHITE);

    snprintf(line, sizeof(line), "Turn:%u", b.turnGene);
    Tft.drawString(line, 30, 240, 2, WHITE);

    Tft.drawString("SELECT = BACK", 58, 266, 1, YELLOW);
  }

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

  for (byte i = 0; i < MAX_BIOMASS_OBJECTS; i++)
  {
    biomassObjects[i].exist = false;
  }

  frameCounter = 0;
  totalBorn = 0;
  totalDead = 0;
  nextBacteriaId = 1;
  simulationEnded = false;

  oldStatusLine1[0] = '\0';

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
  inspectorPage = 0;

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
      selectedBacteriaIndex = (byte)selected;
      inspectorPage = 0;
      showBacteriaInfo(selectedBacteriaIndex);
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
    if (infoScreenVisible)
    {
      inspectorPage = 1 - inspectorPage;
      showBacteriaInfo(selectedBacteriaIndex);
      return;
    }

    if (
      simulationState != STATE_STOPPED &&
      !simulationEnded
    )
    {
      simulationState = STATE_PAUSED;
      selectMode = true;
      infoScreenVisible = false;
      inspectorPage = 0;

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
  nextBacteriaId = 1;

  for (byte i = 0; i < MAX_BACTERIA; i++)
  {
    bacteria[i].alive = false;
  }

  for (byte i = 0; i < MAX_BIOMASS_OBJECTS; i++)
  {
    biomassObjects[i].exist = false;
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
    colonyBehavior(i);
    moveBacteria(i);
    updateAgeAndEnergy(i);
    checkFood(i);
    checkBiomass(i);
    checkAttack(i);
    reproduce(i);
    checkDeath(i);
  }

  drawAllFood();
  drawAllBiomasss();

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