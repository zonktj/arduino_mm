/*
  Sekvensstyrning for myggfangare med termistorovervakning
  Kort: Arduino UNO R3
  Tom Johans 2025-2026

  Huvudkort: Arduino UNO R3
  Fjärrstyrning: Shelley Plus Uni Smart WiFi
  Reläer: Relämodul for Arduino 4x
  Spänningregulator: LM2592s (tar in 12V, justeras till 5V ut)

  Anslutningar:
    Digital pin 2 = Flakt
    Digital pin 3 = Tandstift
    Digital pin 4 = Gasventil
    Analog pin A0  = Termistor/spanningsdelare

  Relakortet antas vara aktivt HIGH:
    HIGH = rela pa
    LOW  = rela av

  Uppmätta riktvärden:
    Kall sensor:       cirka 4,5-4,6 V
    Solvarm sensor:    cirka 4,1 V
    Brinnande gas:     under 2,5 V

  Styrning:
    - Flakt pa i 10 s fore tandning.
    - Tandstift pa i 10 s innan gasen oppnas.
    - Tandstiftet ligger kvar i hogst 20 s efter att gasen oppnats.
    - Under 3,0 V sammanhangande i 5 s bekraftar laga.
    - Ingen bekraftad laga inom 3 min ger saker avstangning,
      10 s efterventilation och nytt startforsok.
    - Hogst tre automatiska omstarter tillats.
    - Over 3,2 V sammanhangande i 30 s betyder att lagan bedoms borta.
    - Om detta sker efter minst 5 min bekraftad drift stoppas systemet.
    - Sensorvarde nara 0 V eller 5 V i 10 s ger permanent stopp.
    - Status loggas pa serieporten varje minut och vid viktiga handelser.

  OBS: Programmet ar en experimentell temperaturbaserad overvakning.
  Det ersatter inte en typgodkand flamvakt, sakerhetsventil eller annan
  skyddsutrustning avsedd for gasol. Använd på egen risk och aldrig inomhus.
*/

// -------------------- Pinnar --------------------
const byte FLAKT_PIN = 2;
const byte TANDSTIFT_PIN = 3;
const byte GAS_PIN = 4;
const byte TERMISTOR_PIN = A0;

// -------------------- Analoga gransvarden --------------------
// Mat garna den verkliga 5 V-matningen och justera vardet vid behov.
const float ADC_REFERENS_V = 5.00;

// Hysteres: lagan tands under den lagre gransen och bedoms borta
// forst over den hogre gransen.
const float FLAMMA_PA_V = 3.00;
const float FLAMMA_AV_V = 3.20;

// Varden mycket nara matningsskenorna kan tyda pa kabelbrott,
// kortslutning eller bortkopplad sensor.
const float SENSOR_MIN_V = 0.05;
const float SENSOR_MAX_V = 4.95;

// -------------------- Tider --------------------
const unsigned long SEKUND = 1000UL;
const unsigned long MINUT = 60000UL;

const unsigned long FORVENTILATION_TID = 10UL * SEKUND;
const unsigned long FORTANDNING_TID = 10UL * SEKUND;
const unsigned long TANDSTIFT_EFTER_GAS_TID = 20UL * SEKUND;
const unsigned long MAX_TANDNINGSTID = 3UL * MINUT;
const unsigned long FLAMMA_BEKRAFTELSE_TID = 5UL * SEKUND;
const unsigned long FLAMMA_BORTA_TID = 30UL * SEKUND;
const unsigned long STABIL_DRIFT_TID = 5UL * MINUT;
const unsigned long EFTERVENTILATION_TID = 10UL * SEKUND;
const unsigned long SENSORFEL_TID = 10UL * SEKUND;
const unsigned long LOGG_INTERVALL = 1UL * MINUT;

// Forsta starten raknas inte som omstart. Darefter tillats tre omstarter.
const byte MAX_OMSTARTER = 3;

// -------------------- Tillstand --------------------
enum Driftlage {
  FORVENTILATION,
  FORTANDNING,
  VANTAR_PA_FLAMMA,
  DRIFT,
  EFTERVENTILATION_OMSTART,
  EFTERVENTILATION_STOPP,
  STOPPAD
};

Driftlage driftlage = STOPPAD;

// -------------------- Globala variabler --------------------
unsigned long lageStartTid = 0;
unsigned long gasStartTid = 0;
unsigned long flammaBekraftadTid = 0;
unsigned long senasteLoggTid = 0;
unsigned long underFlamgransTid = 0;
unsigned long overFlamgransTid = 0;
unsigned long sensorfelStartTid = 0;

bool underFlamgransAktiv = false;
bool overFlamgransAktiv = false;
bool sensorfelAktivt = false;
bool tandstiftAvstangt = false;

byte antalOmstarter = 0;
int adcVarde = 0;
float termistorVolt = 0.0;

// -------------------- Funktionsdeklarationer --------------------
void stangAvAllt();
void lasTermistor();
void startaNySekvens();
void startaEfterventilation(bool skaStartaOm, const __FlashStringHelper *orsak);
void kontrolleraSensorfel(unsigned long nu);
void kontrolleraFlammaVidStart(unsigned long nu);
void kontrolleraFlammaIDrift(unsigned long nu);
void skrivLoggrad(const __FlashStringHelper *meddelande);
const __FlashStringHelper *lageText();

void setup() {
  // Satt utgangarnas latch LOW innan de blir utgangar. Det minskar
  // risken for kort oavsiktlig aktivering vid uppstart.
  digitalWrite(FLAKT_PIN, LOW);
  digitalWrite(TANDSTIFT_PIN, LOW);
  digitalWrite(GAS_PIN, LOW);

  pinMode(FLAKT_PIN, OUTPUT);
  pinMode(TANDSTIFT_PIN, OUTPUT);
  pinMode(GAS_PIN, OUTPUT);
  pinMode(TERMISTOR_PIN, INPUT);

  stangAvAllt();
  Serial.begin(115200);

  Serial.println();
  Serial.println(F("Myggfangare startar"));
  Serial.println(F("Seriehastighet: 115200 baud"));
  Serial.println(F("Flamma PA: < 3.00 V i 5 s"));
  Serial.println(F("Flamma AV: > 3.20 V i 30 s"));

  // Kassera nagra inledande ADC-lasningar.
  for (byte i = 0; i < 10; i++) {
    analogRead(TERMISTOR_PIN);
    delay(10);
  }

  lasTermistor();
  senasteLoggTid = millis();
  skrivLoggrad(F("Uppstart"));
  startaNySekvens();
}

void loop() {
  const unsigned long nu = millis();
  lasTermistor();
  kontrolleraSensorfel(nu);

  // Om sensorfelet redan har startat permanent avstangning ska ingen
  // annan tillstandslogik fa oppna gasen igen under samma loopvarv.
  if (driftlage != EFTERVENTILATION_STOPP && driftlage != STOPPAD) {
    switch (driftlage) {
      case FORVENTILATION:
        if (nu - lageStartTid >= FORVENTILATION_TID) {
          digitalWrite(TANDSTIFT_PIN, HIGH);
          driftlage = FORTANDNING;
          lageStartTid = nu;
          skrivLoggrad(F("Tandstift PA; fortandning startad"));
        }
        break;

      case FORTANDNING:
        if (nu - lageStartTid >= FORTANDNING_TID) {
          digitalWrite(GAS_PIN, HIGH);
          gasStartTid = nu;
          tandstiftAvstangt = false;
          underFlamgransAktiv = false;
          driftlage = VANTAR_PA_FLAMMA;
          lageStartTid = nu;
          skrivLoggrad(F("Gas PA; vantar pa flamma"));
        }
        break;

      case VANTAR_PA_FLAMMA:
        // Tandstiftet stangs av efter 20 s aven om termistorn annu inte
        // hunnit bli varm. Gasen halles oppen medan termistorn far upp
        // till tre minuter pa sig att bekrafta uppvarmningen.
        if (!tandstiftAvstangt &&
            nu - gasStartTid >= TANDSTIFT_EFTER_GAS_TID) {
          digitalWrite(TANDSTIFT_PIN, LOW);
          tandstiftAvstangt = true;
          skrivLoggrad(F("Tandstift AV efter 20 s"));
        }

        kontrolleraFlammaVidStart(nu);

        if (driftlage == VANTAR_PA_FLAMMA &&
            nu - gasStartTid >= MAX_TANDNINGSTID) {
          if (antalOmstarter < MAX_OMSTARTER) {
            startaEfterventilation(true, F("Ingen flamma inom 3 min"));
          } else {
            startaEfterventilation(false, F("Ingen flamma; max omstarter uppnatt"));
          }
        }
        break;

      case DRIFT:
        kontrolleraFlammaIDrift(nu);
        break;

      case EFTERVENTILATION_OMSTART:
      case EFTERVENTILATION_STOPP:
      case STOPPAD:
        // Hanteras utanfor detta switch-block.
        break;
    }
  }

  // Efterventilation hanteras aven efter sensorfel.
  if (driftlage == EFTERVENTILATION_OMSTART &&
      nu - lageStartTid >= EFTERVENTILATION_TID) {
    digitalWrite(FLAKT_PIN, LOW);
    antalOmstarter++;
    Serial.print(F("Automatisk omstart "));
    Serial.print(antalOmstarter);
    Serial.print(F(" av "));
    Serial.println(MAX_OMSTARTER);
    startaNySekvens();
  }

  if (driftlage == EFTERVENTILATION_STOPP &&
      nu - lageStartTid >= EFTERVENTILATION_TID) {
    stangAvAllt();
    driftlage = STOPPAD;
    lageStartTid = nu;
    skrivLoggrad(F("Permanent stopp; manuell aterstallning kravs"));
  }

  if (nu - senasteLoggTid >= LOGG_INTERVALL) {
    senasteLoggTid = nu;
    skrivLoggrad(F("Periodisk logg"));
  }
}

void startaNySekvens() {
  stangAvAllt();

  underFlamgransAktiv = false;
  overFlamgransAktiv = false;
  sensorfelAktivt = false;
  tandstiftAvstangt = false;
  flammaBekraftadTid = 0;

  digitalWrite(FLAKT_PIN, HIGH);
  driftlage = FORVENTILATION;
  lageStartTid = millis();
  skrivLoggrad(F("Flakt PA; forventilation 10 s"));
}

void kontrolleraFlammaVidStart(unsigned long nu) {
  if (termistorVolt < FLAMMA_PA_V) {
    if (!underFlamgransAktiv) {
      underFlamgransAktiv = true;
      underFlamgransTid = nu;
    }

    if (nu - underFlamgransTid >= FLAMMA_BEKRAFTELSE_TID) {
      digitalWrite(TANDSTIFT_PIN, LOW);
      tandstiftAvstangt = true;
      flammaBekraftadTid = nu;
      underFlamgransAktiv = false;
      overFlamgransAktiv = false;
      driftlage = DRIFT;
      lageStartTid = nu;
      skrivLoggrad(F("Flamma bekraftad; normal drift"));
    }
  } else {
    // Femsekundersrakningen maste vara sammanhangande.
    underFlamgransAktiv = false;
  }
}

void kontrolleraFlammaIDrift(unsigned long nu) {
  if (termistorVolt > FLAMMA_AV_V) {
    if (!overFlamgransAktiv) {
      overFlamgransAktiv = true;
      overFlamgransTid = nu;
      skrivLoggrad(F("Varning: mojligt flamavbrott"));
    }

    if (nu - overFlamgransTid >= FLAMMA_BORTA_TID) {
      const unsigned long bekraftadDrifttid = nu - flammaBekraftadTid;

      if (bekraftadDrifttid >= STABIL_DRIFT_TID) {
        // Efter minst fem minuters drift antas t.ex. gasen vara slut.
        // Systemet stoppas och forsoker inte tanda om automatiskt.
        startaEfterventilation(false, F("Flamma borta efter stabil drift"));
      } else if (antalOmstarter < MAX_OMSTARTER) {
        // Tidigt flamavbrott behandlas som instabil start.
        startaEfterventilation(true, F("Flamma borta fore 5 min drift"));
      } else {
        startaEfterventilation(false, F("Tidigt flamavbrott; max omstarter uppnatt"));
      }
    }
  } else {
    // Hysteres: fellarmstimern nollas nar signalen ater ar <= 3,2 V.
    if (overFlamgransAktiv) {
      skrivLoggrad(F("Temperaturen ater inom driftomradet"));
    }
    overFlamgransAktiv = false;
  }
}

void startaEfterventilation(bool skaStartaOm,
                             const __FlashStringHelper *orsak) {
  // Saker ordning: gas och tandstift av forst, flakten kvar pa.
  digitalWrite(GAS_PIN, LOW);
  digitalWrite(TANDSTIFT_PIN, LOW);
  digitalWrite(FLAKT_PIN, HIGH);

  underFlamgransAktiv = false;
  overFlamgransAktiv = false;
  tandstiftAvstangt = true;

  driftlage = skaStartaOm ? EFTERVENTILATION_OMSTART
                          : EFTERVENTILATION_STOPP;
  lageStartTid = millis();
  skrivLoggrad(orsak);
}

void kontrolleraSensorfel(unsigned long nu) {
  const bool orimligt =
      termistorVolt <= SENSOR_MIN_V || termistorVolt >= SENSOR_MAX_V;

  if (orimligt) {
    if (!sensorfelAktivt) {
      sensorfelAktivt = true;
      sensorfelStartTid = nu;
      skrivLoggrad(F("Varning: orimligt sensorvarde"));
    }

    if (nu - sensorfelStartTid >= SENSORFEL_TID &&
        driftlage != EFTERVENTILATION_STOPP &&
        driftlage != STOPPAD) {
      startaEfterventilation(false, F("Sensorfel i minst 10 s"));
    }
  } else {
    sensorfelAktivt = false;
  }
}

void lasTermistor() {
  // Medelvarde av 16 lasningar minskar korta elektriska storningar.
  unsigned long summa = 0;
  for (byte i = 0; i < 16; i++) {
    summa += analogRead(TERMISTOR_PIN);
  }
  adcVarde = (int)(summa / 16UL);
  termistorVolt = adcVarde * (ADC_REFERENS_V / 1023.0);
}

void skrivLoggrad(const __FlashStringHelper *meddelande) {
  Serial.print(F("Tid="));
  Serial.print(millis() / 1000UL);
  Serial.print(F("s ADC="));
  Serial.print(adcVarde);
  Serial.print(F(" Volt="));
  Serial.print(termistorVolt, 3);
  Serial.print(F(" Lage="));
  Serial.print(lageText());
  Serial.print(F(" Flakt="));
  Serial.print(digitalRead(FLAKT_PIN) == HIGH ? F("PA") : F("AV"));
  Serial.print(F(" Tandstift="));
  Serial.print(digitalRead(TANDSTIFT_PIN) == HIGH ? F("PA") : F("AV"));
  Serial.print(F(" Gas="));
  Serial.print(digitalRead(GAS_PIN) == HIGH ? F("PA") : F("AV"));
  Serial.print(F(" Omstarter="));
  Serial.print(antalOmstarter);
  Serial.print(F(" Kommentar="));
  Serial.println(meddelande);
}

const __FlashStringHelper *lageText() {
  switch (driftlage) {
    case FORVENTILATION:
      return F("FORVENTILATION");
    case FORTANDNING:
      return F("FORTANDNING");
    case VANTAR_PA_FLAMMA:
      return F("VANTAR_PA_FLAMMA");
    case DRIFT:
      return F("DRIFT");
    case EFTERVENTILATION_OMSTART:
      return F("EFTERVENTILATION_OMSTART");
    case EFTERVENTILATION_STOPP:
      return F("EFTERVENTILATION_STOPP");
    case STOPPAD:
      return F("STOPPAD");
    default:
      return F("OKANT");
  }
}

void stangAvAllt() {
  // Gasen stangs forst som extra forsiktighetsatgard.
  digitalWrite(GAS_PIN, LOW);
  digitalWrite(TANDSTIFT_PIN, LOW);
  digitalWrite(FLAKT_PIN, LOW);
}
