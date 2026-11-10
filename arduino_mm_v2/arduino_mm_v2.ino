/*
  Sekvensstyrning för myggfångare med temperaturövervakning
  Kort: Arduino UNO R3
  Tom Johans 2025-2026

  Huvudkort: Arduino UNO R3
  Fjärrstyrning: Shelley Plus Uni Smart WiFi
  Reläer: Relämodul for Arduino 4x
  Spänningregulator: LM2592s (tar in 12V, justeras till 5V ut)

  Digital pin 2 = Fläkt
  Digital pin 3 = Tändstift
  Digital pin 4 = Gasventil
  Analog pin A0 = Termistor/spänningsdelare (4,5V kall, ca 3,5V varmt och < 3V drift)

  Reläkortet förutsätts vara aktivt HIGH:
    HIGH = relä på
    LOW  = relä av

  Uppmätta ungefärliga värden:
    Kall sensor:            4,5-4,6 V
    Solvarm sensor:         cirka 4,1 V
    Brinnande gas:          under 2,5 V

  Flamlogik:
    Under 3,0 V i minst 5 sekunder = lågan bekräftad
    Över 3,2 V i minst 30 sekunder = lågan anses slocknad

  OBS:
  Detta är en experimentell temperaturbaserad övervakning.
  Den ersätter inte en godkänd flamvakt eller gasventil med
  egen säkerhetsavstängning. Använd på egen risk och aldrig inomhus.
*/


// ----------------------------------------------------------
// Anslutningar
// ----------------------------------------------------------

const byte FLAKT_PIN     = 2;
const byte TANDSTIFT_PIN = 3;
const byte GAS_PIN       = 4;
const byte TERMISTOR_PIN = A0;


// ----------------------------------------------------------
// Spänningsmätning
// ----------------------------------------------------------

// Arduino UNO använder normalt matningsspänningen som ADC-referens.
// Om den verkliga 5 V-spänningen exempelvis är 4,92 V kan detta
// värde ändras till 4.92 för noggrannare visning.
const float ADC_REFERENS = 5.00;

// Lägre gräns för att bekräfta att lågan har tänt.
const float FLAMMA_PA_VOLT = 3.00;

// Högre gräns för att avgöra att en tidigare bekräftad låga
// har slocknat. Skillnaden mellan 3,0 och 3,2 V ger hysteres.
const float FLAMMA_AV_VOLT = 3.20;


// ----------------------------------------------------------
// Tider
// ----------------------------------------------------------

const unsigned long EN_SEKUND = 1000UL;
const unsigned long EN_MINUT  = 60000UL;

// Fläkten går först i 10 sekunder.
const unsigned long FORVENTILATION_TID = 10UL * EN_SEKUND;

// Därefter går tändstiftet i 10 sekunder innan gasen öppnas.
const unsigned long FOR-TANDNING_TID = 10UL * EN_SEKUND;

// Tändstiftet fortsätter vara aktivt 20 sekunder efter att
// gasen öppnats, enligt den tidigare testsekvensen.
const unsigned long TANDSTIFT_EFTER_GAS_TID = 20UL * EN_SEKUND;

// Lågan måste ha upptäckts inom tre minuter efter att gasen öppnas.
const unsigned long MAX_TANDNINGSTID = 3UL * EN_MINUT;

// Spänningen måste ligga under 3,0 V sammanhängande i fem
// sekunder innan lågan bekräftas.
const unsigned long FLAMMA_BEKRAFTELSE_TID = 5UL * EN_SEKUND;

// Spänningen måste ligga över 3,2 V sammanhängande i 30
// sekunder innan lågan anses ha slocknat.
const unsigned long FLAMMA_BORTA_TID = 30UL * EN_SEKUND;

// Efter fem minuters bekräftad drift betraktas ett senare
// flamavbrott som gas slut eller annat bestående driftfel.
// Systemet stängs då av i stället för att starta om.
const unsigned long STABIL_DRIFT_TID = 5UL * EN_MINUT;

// Fläkten går 10 sekunder efter att gasen stängts.
const unsigned long EFTERVENTILATION_TID = 10UL * EN_SEKUND;

// Logga mätvärden en gång per minut.
const unsigned long LOGG_INTERVALL = 1UL * EN_MINUT;

// Ett orimligt sensorvärde måste finnas i 10 sekunder
// innan det betraktas som ett sensorfel.
const unsigned long SENSORFEL_TID = 10UL * EN_SEKUND;


// ----------------------------------------------------------
// Sensorfelgränser
// ----------------------------------------------------------

// Nära 0 V kan betyda kortslutning.
// Nära 5 V kan betyda kabelbrott eller frånkopplad sensor.
//
// Gränserna är avsiktligt satta utanför de hittills uppmätta
// normalvärdena. Justera dem om verkliga driftprov visar annat.
const float SENSOR_MIN_VOLT = 0.10;
const float SENSOR_MAX_VOLT = 4.90;


// ----------------------------------------------------------
// Begränsning av automatiska startförsök
// ----------------------------------------------------------

// Första starten plus högst tre automatiska omstarter.
// Detta förhindrar att systemet försöker tända oavbrutet.
const byte MAX_OMSTARTER = 3;


// ----------------------------------------------------------
// Drifttillstånd
// ----------------------------------------------------------

enum Driftlage
{
  STARTA_FLAKT,
  STARTA_TANDSTIFT,
  STARTA_GAS,
  VANTAR_PA_FLAMMA,
  DRIFT,
  EFTERVENTILATION_OMSTART,
  EFTERVENTILATION_STOPP,
  STOPPAD
};

Driftlage driftlage = STARTA_FLAKT;


// ----------------------------------------------------------
// Variabler
// ----------------------------------------------------------

unsigned long lageStartTid = 0;
unsigned long senasteLoggTid = 0;

unsigned long underFlamgransSedan = 0;
unsigned long overFlamgransSedan = 0;
unsigned long sensorfelSedan = 0;
unsigned long flammaBekraftadTid = 0;

byte antalOmstarter = 0;

bool flammaBekraftad = false;
bool tandstiftAvstangtEfterGas = false;

int adcVarde = 0;
float termistorVolt = 0.0;


// ----------------------------------------------------------
// Setup
// ----------------------------------------------------------

void setup()
{
  /*
    Skriv LOW innan pinnarna blir utgångar.
    Det minskar risken för en kort oavsiktlig aktivering
    när Arduino startar eller återställs.
  */

  digitalWrite(FLAKT_PIN, LOW);
  digitalWrite(TANDSTIFT_PIN, LOW);
  digitalWrite(GAS_PIN, LOW);

  pinMode(FLAKT_PIN, OUTPUT);
  pinMode(TANDSTIFT_PIN, OUTPUT);
  pinMode(GAS_PIN, OUTPUT);

  pinMode(TERMISTOR_PIN, INPUT);

  Serial.begin(115200);

  stangAvAllt();

  Serial.println();
  Serial.println("========================================");
  Serial.println("Myggfangare startar");
  Serial.println("Termistor pa A0");
  Serial.println("Flamma PA: under 3.00 V");
  Serial.println("Flamma AV: over 3.20 V i 30 sekunder");
  Serial.println("========================================");

  // Gör några ADC-avläsningar för att stabilisera ingången.
  for (byte i = 0; i < 10; i++)
  {
    analogRead(TERMISTOR_PIN);
    delay(10);
  }

  lasTermistor();
  skrivLoggrad("Uppstart");

  startaNySekvens();
}


// ----------------------------------------------------------
// Huvudloop
// ----------------------------------------------------------

void loop()
{
  unsigned long nu = millis();

  // Läs sensorn kontinuerligt.
  lasTermistor();

  // Kontrollera om A0 ligger på ett orimligt värde.
  kontrolleraSensorfel(nu);

  // Skriv normal loggrad en gång per minut.
  if (nu - senasteLoggTid >= LOGG_INTERVALL)
  {
    senasteLoggTid = nu;
    skrivLoggrad("Periodisk logg");
  }

  // Kör aktuell del av start- eller driftsekvensen.
  switch (driftlage)
  {
    case STARTA_FLAKT:
      korStartaFlakt(nu);
      break;

    case STARTA_TANDSTIFT:
      korStartaTandstift(nu);
      break;

    case STARTA_GAS:
      korStartaGas(nu);
      break;

    case VANTAR_PA_FLAMMA:
      korVantarPaFlamma(nu);
      break;

    case DRIFT:
      korDrift(nu);
      break;

    case EFTERVENTILATION_OMSTART:
      korEfterventilationOmstart(nu);
      break;

    case EFTERVENTILATION_STOPP:
      korEfterventilationStopp(nu);
      break;

    case STOPPAD:
      // Allt ska redan vara avstängt.
      // Arduino fortsätter att logga termistorn en gång per minut.
      break;
  }
}


// ----------------------------------------------------------
// Starta en helt ny tändsekvens
// ----------------------------------------------------------

void startaNySekvens()
{
  stangAvAllt();

  flammaBekraftad = false;
  tandstiftAvstangtEfterGas = false;

  underFlamgransSedan = 0;
  overFlamgransSedan = 0;
  flammaBekraftadTid = 0;

  digitalWrite(FLAKT_PIN, HIGH);

  driftlage = STARTA_FLAKT;
  lageStartTid = millis();

  Serial.println();
  Serial.println("Ny startsekvens");
  Serial.println("Flakt PA, forventilation i 10 sekunder");
  skrivLoggrad("Startsekvens");
}


// ----------------------------------------------------------
// Steg 1: Fläkt
// ----------------------------------------------------------

void korStartaFlakt(unsigned long nu)
{
  if (nu - lageStartTid >= FORVENTILATION_TID)
  {
    digitalWrite(TANDSTIFT_PIN, HIGH);

    driftlage = STARTA_TANDSTIFT;
    lageStartTid = nu;

    Serial.println("Tandstift PA");

    