/*
  Sekvensstyrning för myggfangare, test med lysdioder
  Kort: Arduino Uno
  Tom Johans 2025-2026

  Huvudkort Arduino UNO R3
  Fjarrstyrning Shelley Plus Uni Smart WiFi
  Relaer Relamodul for Arduino 4x
  Spanningregulator LM2592s (tar in 12V, ska ge ut 5V)

  Digital pin 2 = Flakt
  Digital pin 3 = Tandstift
  Digital pin 4 = Gas, endast lysdiod i detta test

  Forutsattning:
  Relakortet ar aktivt HIGH:
  HIGH = rela/lysdiod pa
  LOW  = rela/lysdiod av
*/

const byte FLAKT_PIN = 2;
const byte TANDSTIFT_PIN = 3;
const byte GAS_PIN = 4;

void setup()
{
  /*
    Skriv LOW innan pinnarna blir utgangar.
    Det minskar risken for en kort oavsiktlig aktivering
    nar Arduino startar.
  */
  digitalWrite(FLAKT_PIN, LOW);
  digitalWrite(TANDSTIFT_PIN, LOW);
  digitalWrite(GAS_PIN, LOW);

  pinMode(FLAKT_PIN, OUTPUT);
  pinMode(TANDSTIFT_PIN, OUTPUT);
  pinMode(GAS_PIN, OUTPUT);

  // Sakerstall att allt ar avstangt.
  stangAvAllt();

  // Steg 1: Starta flakten direkt.
  digitalWrite(FLAKT_PIN, HIGH);

  // Vanta 10 sekunder.
  delay(10000UL);

  // Steg 2: Starta tandstiftet.
  // Flakten fortsatter vara aktiv.
  digitalWrite(TANDSTIFT_PIN, HIGH);

  // Vanta ytterligare 10 sekunder.
  delay(10000UL);

  // Steg 3: Aktivera gasutgangen.
  // Flakt och tandstift fortsatter vara aktiva.
  digitalWrite(GAS_PIN, HIGH);

  // Vanta 20 sekunder med alla tre utgangar aktiva.
  delay(20000UL);

  // Steg 4: Stang av tandstiftet.
  digitalWrite(TANDSTIFT_PIN, LOW);

  /*
    Slutlage:
    Flakt     = HIGH
    Tandstift = LOW
    Gas       = HIGH

    Detta lage halls kvar sa lange Arduino har strom.
  */
}

void loop()
{
  /*
    Ingen upprepad funktion behovs just nu.
    Utgangarna behaller slutlaget fran setup().
  */
}

void stangAvAllt()
{
  digitalWrite(FLAKT_PIN, LOW);
  digitalWrite(TANDSTIFT_PIN, LOW);
  digitalWrite(GAS_PIN, LOW);
}