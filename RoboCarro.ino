// RoboCarro - Arduino Nano
// Sensores: MQ135 (ar), sensor de chama IR, ultrassonico HC-SR04, LCD 16x2 I2C, buzzer passivo
// Sons: fogo = sirene aguda rapida | ar ruim = sirene grave | proximidade = bipes (sensor de re)
// Motores via ponte H (L298N). Controle pela Serial (USB, 115200) ou pelo
// celular com o app Dabble (Android), modulo GamePad, via Bluetooth.
//
// Comandos USB: w=frente s=re a=esquerda d=direita x/espaco=parar
//               1-9=velocidade m=mudo c=calibrar ar t=telemetria h=ajuda
//
// Dabble GamePad: setas = andar (segure; soltou, parou)
//                 triangulo = +velocidade  X = -velocidade  quadrado = mudo
//                 select = calibrar ar     start = parar
//
// Ligacao do modulo Bluetooth HC-05 / HC-06 (serial 9600, padrao dos modulos):
//   HC VCC   -> 5V
//   HC GND   -> GND
//   HC TXD   -> D2 (RX do Arduino), ligacao direta
//   HC RXD   <- D4 (TX do Arduino) por divisor: D4 -[1k]- RXD -[2k]- GND
//               (o RXD do modulo e 3,3 V; sem o divisor pode queimar)
//   HC STATE -> A6 (opcional, ver BT_USA_STATE)
// Nao use D0/D1: sao a Serial da USB (upload e controle pelo PC).
// Biblioteca: "Dabble" (STEMpedia), pelo Gerenciador de Bibliotecas.

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Inclui so o modulo GamePad do Dabble (sem isso entram todos e falta RAM).
#define CUSTOM_SETTINGS
#define INCLUDE_GAMEPAD_MODULE
#include <Dabble.h>

// ---------- Pinos ----------
const uint8_t PIN_MQ135        = A0;
const uint8_t PIN_LED_VERMELHO = A1;
const uint8_t PIN_LED_AMARELO  = A2;
const uint8_t PIN_LED_VERDE    = A3;
const uint8_t PIN_IR           = A7;  // sensor de chama; A7 e so analogico no Nano
const uint8_t PIN_BUZZER       = 3;
const uint8_t PIN_TRIG         = 5;
const uint8_t PIN_ECHO         = 12;
const uint8_t PIN_ENA          = 6;
const uint8_t PIN_IN1          = 7;
const uint8_t PIN_IN2          = 8;
const uint8_t PIN_IN3          = 9;
const uint8_t PIN_IN4          = 11;  // D11 fica so digital (tone() usa o Timer2)
const uint8_t PIN_ENB          = 10;  // PWM no Timer1, livre do buzzer
const uint8_t PIN_BT_RX        = 2;   // <- TXD do HC-05 (SoftwareSerial)
const uint8_t PIN_BT_TX        = 4;   // -> RXD do HC-05 (com divisor); D3 e o buzzer
const uint8_t PIN_BT_STATE     = A6;  // STATE do HC-05; A6 e so analogico no Nano

// ---------- Ajustes ----------
const bool INVERTE_MOTOR_A = true;   // troque se o motor A girar ao contrario
const bool INVERTE_MOTOR_B = false;  // troque se o motor B girar ao contrario
const bool MOTOR_A_ESQUERDO = true;  // false se o motor A for o da direita

const int DIST_PARADA_CM = 15;  // bloqueia frente e apita continuo
const int DIST_ALERTA_CM = 40;  // comeca a apitar (estilo sensor de re)

const unsigned long AQUECIMENTO_MS = 15000;  // aquecimento do MQ135 (motores liberados)
const int GAS_DELTA_MEDIO = 60;   // leitura acima da base -> amarelo
const int GAS_DELTA_RUIM  = 150;  // leitura acima da base -> vermelho + bipe
const int GAS_HISTERESE   = 15;
const float GAS_ADAPTA_BASE = 0.002;  // base acompanha deriva lenta (~50 s) com ar bom

// Troca de alimentacao (USB <-> ponte H) ou queda nos motores muda a tensao do
// aquecedor do MQ135 e a leitura salta. Nesses casos a avaliacao do ar congela
// ate a tensao ficar estavel e o salto e descontado da base.
const int VCC_SALTO_MV = 120;
const unsigned long VCC_ESTAVEL_MS = 8000;

// Os motores puxam corrente e a leitura do MQ135 flutua: enquanto andam (e um
// pouco depois) o nivel do ar fica congelado no ultimo valor.
const unsigned long AR_PAUSA_MOTOR_MS = 3000;

const int  IR_LIMIAR = 500;        // leitura analogica do A7
const bool IR_ATIVO_BAIXO = true;  // modulo de chama vai a LOW ao detectar

const uint8_t VEL_PWM_MIN = 110;  // abaixo disso a L298N geralmente nao move
const uint8_t VEL_PWM_MAX = 255;

// Seguranca (so USB): para se nenhum comando de movimento chegar nesse tempo. O
// controle do PC reenvia o comando enquanto a tecla esta pressionada (0 desativa).
const unsigned long TIMEOUT_COMANDO_MS = 400;

// Bluetooth / Dabble
const long BT_BAUD = 9600;  // tem que ser igual a do modulo (HC-05/HC-06 vem em 9600)
// O Dabble so envia dados quando um botao muda (aperta/solta). Se a conexao cair
// com uma seta pressionada, o robo nao fica sabendo. Ligando o pino STATE do
// HC-05 no A6 e colocando true aqui, ele para quando a conexao cai.
// Deixe false se nao ligar o STATE (A6 solto daria leituras aleatorias).
const bool BT_USA_STATE = false;
const int  BT_STATE_LIMIAR = 400;  // ~2 V; STATE fica em ~3,3 V quando conectado

// ---------- Estado ----------
enum Movimento { PARADO, FRENTE, RE, ESQUERDA, DIREITA };
const char *const NOMES_MOV[] = {"PARADO", "FRENTE", "RE", "ESQ", "DIR"};

enum NivelAr { AR_BOM, AR_MEDIO, AR_RUIM };
const char *const NOMES_AR[] = {"BOM", "MED", "RUIM"};

LiquidCrystal_I2C *lcd = nullptr;

Movimento movimento = PARADO;
uint8_t nivelVel = 5;
bool mudo = false;
bool telemetria = true;

int distanciaCm = -1;  // -1 = sem eco / fora de alcance
uint8_t leiturasPerto = 0;
bool chama = false;

float gasFiltrado = 0;
float gasBase = 0;
bool aquecendo = true;
NivelAr nivelAr = AR_BOM;

int vccMv = 0, vccEstavelMv = 0;
bool estabilizando = false;
float gasAntesSalto = 0;
unsigned long tVccMudou = 0;

bool frenteBloqueada = false;
bool controleBt = false;  // true: o movimento atual e comandado pelo celular
Movimento movBtAnterior = PARADO;
uint8_t botoesBtAnterior = 0;
bool btConectado = false;
unsigned long tUltimoComando = 0;
unsigned long tMotorAtivo = 0;  // ultima vez em que os motores estavam ligados

unsigned long bipeAte = 0;
unsigned long tUltra = 0, tGas = 0, tLcd = 0, tTele = 0;

// ---------- Buzzer (passivo) ----------
// tone() usa o Timer2; por isso o ENB foi para o D10 (Timer1).
void setTom(unsigned int freq) {
  static unsigned int atual = 0;
  if (freq == atual) return;
  atual = freq;
  if (freq) tone(PIN_BUZZER, freq);
  else noTone(PIN_BUZZER);
}

void bipe(unsigned int ms) {
  if (!mudo) bipeAte = millis() + ms;
}

void atualizaBuzzer(unsigned long agora) {
  unsigned int freq = 0;
  if (agora < bipeAte) {
    freq = 2000;
  } else if (!mudo) {
    if (chama) {
      // sirene aguda e rapida
      freq = (agora / 120) % 2 ? 3200 : 2400;
    } else if (!aquecendo && nivelAr == AR_RUIM) {
      // sirene grave de dois tons com pausa
      unsigned long fase = agora % 1200;
      if (fase < 300) freq = 700;
      else if (fase < 600) freq = 500;
    } else if (distanciaCm > 0 && distanciaCm < DIST_ALERTA_CM) {
      // estilo sensor de re: mais perto = mais agudo e mais rapido
      unsigned int tom = map(distanciaCm, 0, DIST_ALERTA_CM, 3000, 1200);
      if (distanciaCm <= DIST_PARADA_CM) {
        freq = tom;
      } else {
        unsigned long periodo = map(distanciaCm, DIST_PARADA_CM, DIST_ALERTA_CM, 120, 700);
        if (agora % periodo < 60) freq = tom;
      }
    }
  }
  setTom(freq);
}

// ---------- Motores ----------
void acionaMotor(uint8_t en, uint8_t inA, uint8_t inB, int vel, bool inverte) {
  if (inverte) vel = -vel;
  digitalWrite(inA, vel > 0 ? HIGH : LOW);
  digitalWrite(inB, vel < 0 ? HIGH : LOW);
  analogWrite(en, abs(vel));
}

void aplicaMotores() {
  int v = map(nivelVel, 1, 9, VEL_PWM_MIN, VEL_PWM_MAX);
  int esq = 0, dir = 0;
  switch (movimento) {
    case FRENTE:   esq = v;  dir = v;  break;
    case RE:       esq = -v; dir = -v; break;
    case ESQUERDA: esq = -v; dir = v;  break;
    case DIREITA:  esq = v;  dir = -v; break;
    default: break;
  }
  int velA = MOTOR_A_ESQUERDO ? esq : dir;
  int velB = MOTOR_A_ESQUERDO ? dir : esq;
  acionaMotor(PIN_ENA, PIN_IN1, PIN_IN2, velA, INVERTE_MOTOR_A);
  acionaMotor(PIN_ENB, PIN_IN3, PIN_IN4, velB, INVERTE_MOTOR_B);
}

bool obstaculoPerto() {
  return distanciaCm > 0 && distanciaCm <= DIST_PARADA_CM;
}

// Chamado a cada comando recebido; o controle reenvia o mesmo comando
// varias vezes por segundo, entao so age/imprime quando algo muda.
void setMovimento(Movimento m) {
  tUltimoComando = millis();
  if (m == FRENTE && obstaculoPerto()) {
    if (!frenteBloqueada) {
      Serial.println(F("! Obstaculo a frente - frente bloqueada"));
      bipe(150);
    }
    frenteBloqueada = true;
    m = PARADO;
  } else {
    frenteBloqueada = false;
  }
  if (m == movimento) return;
  movimento = m;
  aplicaMotores();
  Serial.print(F("> "));
  Serial.println(NOMES_MOV[m]);
}

// ---------- Sensores ----------
void lerUltrassom() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  // pulseInLong mede pelo micros(): a interrupcao do SoftwareSerial (Bluetooth)
  // nao encurta a medida, como aconteceria com o pulseIn.
  unsigned long us = pulseInLong(PIN_ECHO, HIGH, 25000UL);  // ~4 m
  distanciaCm = us == 0 ? -1 : us / 58;

  // Para sozinho se estiver indo para frente e houver obstaculo
  // (duas leituras seguidas para ignorar ruido).
  leiturasPerto = obstaculoPerto() ? leiturasPerto + 1 : 0;
  if (movimento == FRENTE && leiturasPerto >= 2) {
    movimento = PARADO;
    aplicaMotores();
    Serial.println(F("! Obstaculo - parada automatica"));
  }
}

void lerChama() {
  bool baixo = analogRead(PIN_IR) < IR_LIMIAR;
  bool detectou = IR_ATIVO_BAIXO ? baixo : !baixo;
  if (detectou != chama) {
    chama = detectou;
    Serial.println(chama ? F("! FOGO detectado") : F("Chama apagada"));
  }
}

void calibraAr() {
  aquecendo = false;
  gasBase = gasFiltrado;
  nivelAr = AR_BOM;
  Serial.print(F("MQ135 calibrado. Base = "));
  Serial.println((int)round(gasBase));
}

// Mede o proprio Vcc usando a referencia interna de 1,1 V.
int lerVccMv() {
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2);  // assentar a referencia
  ADCSRA |= _BV(ADSC);
  while (bit_is_set(ADCSRA, ADSC)) {}
  return 1125300L / ADC;  // 1.1 V * 1023 * 1000
}

// Retorna true enquanto a alimentacao ainda esta se acomodando.
bool verificaVcc(unsigned long agora) {
  vccMv += (lerVccMv() - vccMv) / 2;
  if (abs(vccMv - vccEstavelMv) > VCC_SALTO_MV) {
    if (!estabilizando) {
      estabilizando = true;
      gasAntesSalto = gasFiltrado;
      Serial.print(F("Alimentacao mudou ("));
      Serial.print(vccEstavelMv);
      Serial.print(F(" -> "));
      Serial.print(vccMv);
      Serial.println(F(" mV), estabilizando ar..."));
    }
    vccEstavelMv = vccMv;
    tVccMudou = agora;
  }
  if (estabilizando && agora - tVccMudou >= VCC_ESTAVEL_MS) {
    estabilizando = false;
    if (!aquecendo) gasBase += gasFiltrado - gasAntesSalto;
    Serial.println(F("Ar estabilizado"));
  }
  return estabilizando;
}

void lerGas(unsigned long agora) {
  gasFiltrado += (analogRead(PIN_MQ135) - gasFiltrado) * 0.2;
  if (verificaVcc(agora)) return;
  if (aquecendo) {
    if (agora >= AQUECIMENTO_MS) calibraAr();
    return;
  }
  if (movimento != PARADO || agora - tMotorAtivo < AR_PAUSA_MOTOR_MS) return;

  int delta = round(gasFiltrado - gasBase);
  // Com ar bom a base segue devagar a deriva do sensor (ele continua
  // esquentando por minutos); mudancas rapidas de gas ainda disparam.
  if (nivelAr == AR_BOM && delta < GAS_DELTA_MEDIO / 2) gasBase += (gasFiltrado - gasBase) * GAS_ADAPTA_BASE;
  NivelAr novo = delta >= GAS_DELTA_RUIM ? AR_RUIM : delta >= GAS_DELTA_MEDIO ? AR_MEDIO : AR_BOM;
  if (novo < nivelAr) {  // melhorando: so desce depois de passar a histerese
    int limiar = nivelAr == AR_RUIM ? GAS_DELTA_RUIM : GAS_DELTA_MEDIO;
    if (delta > limiar - GAS_HISTERESE) novo = nivelAr;
  }
  if (novo != nivelAr) {
    nivelAr = novo;
    Serial.print(F("Ar: "));
    Serial.println(NOMES_AR[nivelAr]);
  }
}

void atualizaLeds(unsigned long agora) {
  bool vermelho = false, amarelo = false, verde = false;
  if (chama) {
    vermelho = (agora / 120) % 2;  // alterna no ritmo da sirene de fogo
    amarelo = !vermelho;
  } else if (aquecendo || estabilizando) {
    amarelo = (agora / 500) % 2;  // pisca enquanto aquece
  } else {
    verde = nivelAr == AR_BOM;
    amarelo = nivelAr == AR_MEDIO;
    vermelho = nivelAr == AR_RUIM;
  }
  digitalWrite(PIN_LED_VERMELHO, vermelho);
  digitalWrite(PIN_LED_AMARELO, amarelo);
  digitalWrite(PIN_LED_VERDE, verde);
}

// ---------- LCD ----------
void iniciaLcd() {
  Wire.begin();
  Wire.setWireTimeout(3000, true);
  for (uint8_t addr = 0x20; addr <= 0x3F; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      lcd = new LiquidCrystal_I2C(addr, 16, 2);
      lcd->init();
      lcd->backlight();
      Serial.print(F("LCD encontrado em 0x"));
      Serial.println(addr, HEX);
      return;
    }
  }
  Serial.println(F("! LCD I2C nao encontrado (confira SDA=A4, SCL=A5)"));
}

void imprimeLinha(uint8_t linha, const char *texto) {
  lcd->setCursor(0, linha);
  uint8_t n = 0;
  for (; texto[n] && n < 16; n++) lcd->write(texto[n]);
  for (; n < 16; n++) lcd->write(' ');
}

void atualizaLcd(unsigned long agora) {
  if (!lcd) return;
  char l1[17], l2[17], dist[4];

  if (chama) {
    strcpy(l1, "!!! FOGO !!!");
  } else if (aquecendo) {
    unsigned long resta = agora < AQUECIMENTO_MS ? (AQUECIMENTO_MS - agora) / 1000 + 1 : 0;
    snprintf(l1, sizeof l1, "Aquecendo MQ %2lus", resta);
  } else if (estabilizando) {
    strcpy(l1, "Estabilizando ar");
  } else {
    snprintf(l1, sizeof l1, "Ar:%-4s G:%d", NOMES_AR[nivelAr], (int)round(gasFiltrado));
  }

  if (distanciaCm < 0) strcpy(dist, "---");
  else snprintf(dist, sizeof dist, "%3d", min(distanciaCm, 999));
  snprintf(l2, sizeof l2, "%scm %-6s V%u%c", dist, NOMES_MOV[movimento], nivelVel, mudo ? 'M' : ' ');

  imprimeLinha(0, l1);
  imprimeLinha(1, l2);
}

// ---------- Comandos ----------
void ajuda() {
  Serial.println(F("Comandos: w=frente s=re a=esq d=dir x/espaco=parar"));
  Serial.println(F("          1-9=velocidade m=mudo c=calibrar ar t=telemetria h=ajuda"));
  Serial.println(F("Dabble GamePad: setas=andar triangulo/X=+/-vel quadrado=mudo"));
  Serial.println(F("                select=calibrar ar start=parar"));
}

void processaComando(char c) {
  if (c >= '1' && c <= '9') {
    nivelVel = c - '0';
    aplicaMotores();
    Serial.print(F("> Velocidade "));
    Serial.println(nivelVel);
    return;
  }
  c = tolower(c);
  if (c && strchr("wsadx ", c)) controleBt = false;  // comando pela USB assume o controle
  switch (c) {
    case 'w': setMovimento(FRENTE); break;
    case 's': setMovimento(RE); break;
    case 'a': setMovimento(ESQUERDA); break;
    case 'd': setMovimento(DIREITA); break;
    case 'x':
    case ' ': setMovimento(PARADO); break;
    case 'm':
      mudo = !mudo;
      Serial.println(mudo ? F("> Buzzer mudo") : F("> Buzzer ligado"));
      break;
    case 'c': calibraAr(); break;
    case 't': telemetria = !telemetria; break;
    case 'h':
    case '?': ajuda(); break;
    default: break;  // ignora \r, \n e o resto
  }
}

// ---------- Bluetooth (Dabble GamePad) ----------
Movimento movimentoGamePad() {
  if (GamePad.isUpPressed()) return FRENTE;
  if (GamePad.isDownPressed()) return RE;
  if (GamePad.isLeftPressed()) return ESQUERDA;
  if (GamePad.isRightPressed()) return DIREITA;
  // Modo joystick/acelerometro do app: angulo 0=direita 90=frente, raio 0-7
  if (GamePad.getRadius() >= 3) {
    uint16_t ang = GamePad.getAngle();
    if (ang >= 45 && ang < 135) return FRENTE;
    if (ang >= 135 && ang < 225) return ESQUERDA;
    if (ang >= 225 && ang < 315) return RE;
    return DIREITA;
  }
  return PARADO;
}

void lerBluetooth() {
  Dabble.processInput();

  static bool avisouApp = false;
  if (!avisouApp && Dabble.isAppConnected()) {
    avisouApp = true;
    Serial.println(F("Celular conectado (Dabble)"));
    bipe(80);
  }

  if (BT_USA_STATE) {
    bool conectado = analogRead(PIN_BT_STATE) > BT_STATE_LIMIAR;
    if (conectado != btConectado) {
      btConectado = conectado;
      Serial.println(conectado ? F("Bluetooth conectado") : F("! Bluetooth desconectado"));
      if (!conectado && controleBt) {
        controleBt = false;
        setMovimento(PARADO);
      }
    }
    if (!conectado) return;  // o GamePad guarda o ultimo estado recebido; ignora
  }

  // O app so envia quando um botao muda, entao o estado lido vale enquanto
  // a seta estiver pressionada. Qualquer mudanca passa o controle ao celular.
  Movimento m = movimentoGamePad();
  if (m != movBtAnterior) {
    movBtAnterior = m;
    controleBt = true;
  }
  // Reaplica a cada volta: se a frente estava bloqueada, retoma quando o
  // obstaculo sair (como o controle do PC, que reenvia o comando).
  if (controleBt) setMovimento(m);

  // Botoes agem so no instante em que sao pressionados.
  uint8_t botoes = GamePad.isTrianglePressed()      | GamePad.isCrossPressed() << 1 |
                   GamePad.isSquarePressed() << 2  | GamePad.isSelectPressed() << 3 |
                   GamePad.isStartPressed() << 4;
  uint8_t novos = botoes & ~botoesBtAnterior;
  botoesBtAnterior = botoes;
  if ((novos & 0x01) && nivelVel < 9) processaComando('1' + nivelVel);      // triangulo
  if ((novos & 0x02) && nivelVel > 1) processaComando('0' + nivelVel - 1);  // X
  if (novos & 0x04) processaComando('m');  // quadrado
  if (novos & 0x08) processaComando('c');  // select
  if (novos & 0x10) processaComando('x');  // start
}

void enviaTelemetria() {
  Serial.print(F("T dist="));
  Serial.print(distanciaCm);
  Serial.print(F(" gas="));
  Serial.print((int)round(gasFiltrado));
  Serial.print(F(" base="));
  Serial.print((int)round(gasBase));
  Serial.print(F(" ar="));
  Serial.print(aquecendo ? "AQUEC" : NOMES_AR[nivelAr]);
  Serial.print(F(" fogo="));
  Serial.print(chama);
  Serial.print(F(" vcc="));
  Serial.print(vccMv);
  Serial.print(F(" irRaw="));
  Serial.print(analogRead(PIN_IR));
  Serial.print(F(" mov="));
  Serial.print(NOMES_MOV[movimento]);
  Serial.print(F(" vel="));
  Serial.println(nivelVel);
}

// ---------- Setup / loop ----------
void setup() {
  const uint8_t saidas[] = {PIN_LED_VERMELHO, PIN_LED_AMARELO, PIN_LED_VERDE, PIN_BUZZER, PIN_TRIG,
                            PIN_ENA, PIN_IN1, PIN_IN2, PIN_IN3, PIN_IN4, PIN_ENB};
  for (uint8_t p : saidas) pinMode(p, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  aplicaMotores();  // garante motores parados

  Serial.begin(115200);
  Serial.println(F("RoboCarro iniciado"));
  Dabble.begin(BT_BAUD, PIN_BT_RX, PIN_BT_TX);  // SoftwareSerial do Bluetooth
  iniciaLcd();
  ajuda();

  // Teste dos LEDs: vermelho, amarelo, verde (um por vez)
  const uint8_t leds[] = {PIN_LED_VERMELHO, PIN_LED_AMARELO, PIN_LED_VERDE};
  for (uint8_t p : leds) {
    digitalWrite(p, HIGH);
    delay(400);
    digitalWrite(p, LOW);
  }

  gasFiltrado = analogRead(PIN_MQ135);
  vccMv = vccEstavelMv = lerVccMv();
  bipe(80);
}

void loop() {
  unsigned long agora = millis();

  // Antes de ler a Serial: um comando lido agora teria tUltimoComando > agora.
  // Nao vale para o celular: o Dabble nao reenvia enquanto o botao esta apertado.
  if (TIMEOUT_COMANDO_MS && !controleBt && movimento != PARADO &&
      agora - tUltimoComando > TIMEOUT_COMANDO_MS) {
    movimento = PARADO;
    aplicaMotores();
    Serial.println(F("> PARADO (sem comando)"));
  }
  while (Serial.available()) processaComando(Serial.read());
  lerBluetooth();
  if (movimento != PARADO) tMotorAtivo = agora;

  if (agora - tUltra >= 60) {
    tUltra = agora;
    lerUltrassom();
  }
  if (agora - tGas >= 100) {
    tGas = agora;
    lerGas(agora);
    lerChama();
  }
  atualizaLeds(agora);
  atualizaBuzzer(agora);

  if (agora - tLcd >= 250) {
    tLcd = agora;
    atualizaLcd(agora);
  }
  if (telemetria && agora - tTele >= 1000) {
    tTele = agora;
    enviaTelemetria();
  }
}
