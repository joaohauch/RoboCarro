# RoboCarro

Carrinho robô com Arduino Nano que monitora o ambiente e avisa com luz, som e display. Ele mede a qualidade do ar, detecta fogo e obstáculos. Pode ser dirigido pelo celular, com o app **Dabble** via Bluetooth, ou pelo teclado do PC, via USB.

## O que ele faz

- **Movimento:** 2 motores DC com ponte H L298N e 9 níveis de velocidade.
- **Qualidade do ar (MQ135):**
  - aquece por 15 s e se calibra sozinho;
  - LEDs verde, amarelo e vermelho mostram o nível do ar;
  - com ar ruim, toca uma sirene grave.
- **Fogo (sensor de chama IR):** sirene aguda, LEDs piscando e "FOGO" no display.
- **Obstáculos (HC-SR04):**
  - abaixo de 40 cm, bipa como um sensor de ré: quanto mais perto, mais rápido e mais agudo;
  - a 15 cm, o som fica contínuo, o carro para sozinho e não deixa seguir em frente.
- **Display LCD 16x2 I2C:** mostra o nível do ar, a distância, o movimento atual e a velocidade.
- **Segurança:** no controle pelo PC, o carro para se ficar 400 ms sem receber comando, por exemplo se o cabo ou o programa caírem.

## Arquivos

| Arquivo | Conteúdo |
|---|---|
| [`RoboCarro.ino`](RoboCarro.ino) | Firmware do Arduino Nano |
| [`controle.py`](controle.py) | Controle pelo teclado do PC (Windows) |

## Material

- Arduino Nano (ATmega328P)
- Ponte H L298N, 2 motores DC com rodas e bateria para os motores
- Módulo Bluetooth HC-06 ou HC-05
- Sensor de gás MQ135
- Sensor de chama IR (módulo com saída analógica)
- Sensor ultrassônico HC-SR04
- Display LCD 16x2 com módulo I2C (PCF8574)
- Buzzer **passivo**
- 3 LEDs (vermelho, amarelo e verde) com resistores de 220 a 330 Ω
- Resistores de 1 kΩ e 2 kΩ (ou 2,2 kΩ) para o divisor de tensão do Bluetooth

## Ligações

| Pino do Nano | Ligado em |
|---|---|
| A0 | MQ135, saída analógica (AO) |
| A1 | LED vermelho |
| A2 | LED amarelo |
| A3 | LED verde |
| A4 | LCD I2C, SDA |
| A5 | LCD I2C, SCL |
| A6 | STATE do HC-05 (opcional, ver [Ajustes](#ajustes)) |
| A7 | Sensor de chama, saída analógica (AO) |
| D2 | TXD do módulo Bluetooth (ligação direta) |
| D3 | Buzzer passivo |
| D4 | RXD do módulo Bluetooth, **pelo divisor de tensão** |
| D5 | HC-SR04, TRIG |
| D6 | L298N, ENA |
| D7 | L298N, IN1 |
| D8 | L298N, IN2 |
| D9 | L298N, IN3 |
| D10 | L298N, ENB |
| D11 | L298N, IN4 |
| D12 | HC-SR04, ECHO |

### Módulo Bluetooth

| HC-06 / HC-05 | Nano |
|---|---|
| VCC | **5V** (o 3V3 do Nano tem pouca corrente e a conexão pode falhar) |
| GND | GND |
| TXD | D2 |
| RXD | D4, pelo divisor abaixo |

O RXD do módulo trabalha em 3,3 V. Ligar os 5 V do Arduino direto pode queimar o módulo, por isso o divisor:

```
D4 ──[1 kΩ]──┬── RXD do módulo
             │
          [2 kΩ]
             │
            GND
```

O Bluetooth fica em D2/D4 porque D0/D1 são a serial da USB. Assim dá para gravar o código e usar o controle do PC com o módulo ligado.

### Ponte H L298N

- **Tire os jumpers de ENA e ENB** da placa. Sem isso, o PWM do Arduino não controla a velocidade.
- Ligue o **GND da L298N no GND do Arduino**.
- Pela USB, só o Arduino é alimentado. Os motores precisam da bateria ligada na L298N.

### Por que esses pinos

- `tone()`, usado no buzzer, ocupa o Timer2. Isso tira o PWM dos pinos D3 e D11. Por isso o ENB fica no D10 (Timer1) e o D11 é usado só como saída digital (IN4).
- O A6 e o A7 do Nano são só analógicos. Servem para o sensor de chama e para o STATE do Bluetooth, que são lidos com `analogRead`.

## Bibliotecas

Instale pelo Gerenciador de Bibliotecas da Arduino IDE:

- **LiquidCrystal I2C** (Frank de Brabander)
- **Dabble** (STEMpedia)

Ou pela linha de comando:

```
arduino-cli lib install "LiquidCrystal I2C" Dabble
```

## Gravar no Arduino

A Arduino IDE exige que a pasta tenha o mesmo nome do `.ino`. Se você baixou o projeto como ZIP, renomeie a pasta `RoboCarro-main` para `RoboCarro`.

Na Arduino IDE:

1. Abra `RoboCarro.ino`.
2. Escolha a placa **Arduino Nano**.
3. Escolha o processador **ATmega328P**. Se a gravação falhar, use **ATmega328P (Old Bootloader)**, comum em placas genéricas.
4. Escolha a porta COM do Nano e clique em Carregar.

Ou com o `arduino-cli`, dentro da pasta do projeto (troque COM6 pela sua porta):

```
arduino-cli compile --upload -b arduino:avr:nano -p COM6 .
```

## Controle pelo celular (Dabble)

1. Instale o **Dabble** (STEMpedia) pela Play Store.
2. Ligue o robô. O LED do HC-06 fica piscando.
3. Pareie o celular com o módulo em **Configurações → Bluetooth**. A senha padrão é `1234` ou `0000`.
4. Abra o Dabble e aceite as permissões que ele pedir: "Dispositivos próximos" ou Localização.
5. Toque no ícone de conexão e escolha o módulo. O LED do HC-06 fica aceso fixo.
6. Abra o módulo **GamePad**. No primeiro botão apertado, o robô bipa e escreve "Celular conectado (Dabble)" na serial.

| Botão no GamePad | Ação |
|---|---|
| Setas | Andar (segure; soltou, parou) |
| Triângulo | Aumenta a velocidade |
| X | Diminui a velocidade |
| Quadrado | Liga/desliga o som (mudo) |
| Select | Calibra o sensor de ar |
| Start | Para |

O modo joystick do GamePad também funciona.

O Dabble só envia dados quando um botão é apertado ou solto. Por isso, se a conexão cair com uma seta apertada, o robô não fica sabendo e continua andando. Só a frente para sozinha, pelo sensor ultrassônico. Para o robô parar quando a conexão cair, veja `BT_USA_STATE` em [Ajustes](#ajustes).

## Controle pelo PC (USB)

Requer Python 3 com `pyserial`:

```
pip install pyserial
python controle.py COM6
```

- Segure as setas ou WASD para andar. Soltou, parou.
- O programa só lê o teclado quando a janela dele está em foco, para o carro não andar enquanto você digita em outro lugar.
- `q` ou Esc fecha o programa.
- Ao conectar, o Arduino reinicia.

Também dá para mandar os comandos pelo Monitor Serial da IDE, a **115200** baud:

| Comando | Ação |
|---|---|
| `w` `s` `a` `d` | Frente, ré, esquerda, direita |
| `x` ou espaço | Parar |
| `1` a `9` | Velocidade |
| `m` | Liga/desliga o som (mudo) |
| `c` | Calibra o sensor de ar (use com o ar limpo) |
| `t` | Liga/desliga a telemetria |
| `h` ou `?` | Ajuda |

Pelo Monitor Serial o carro anda só 400 ms por comando, por causa da parada de segurança. O `controle.py` reenvia o comando enquanto a tecla está apertada.

A telemetria, uma linha por segundo, mostra os valores dos sensores:

```
T dist=37 gas=512 base=498 ar=BOM fogo=0 vcc=4807 irRaw=1023 mov=PARADO vel=5
```

| Campo | Significado |
|---|---|
| `dist` | Distância em cm (`-1` = sem eco) |
| `gas` | Leitura do MQ135 |
| `base` | Valor de referência do ar limpo |
| `ar` | Nível do ar (`AQUEC` enquanto aquece) |
| `fogo` | `1` se detectou chama |
| `vcc` | Tensão de alimentação do Nano, em mV |
| `irRaw` | Leitura crua do sensor de chama |
| `mov` | Movimento atual |
| `vel` | Nível de velocidade |

## Ajustes

As constantes ficam no começo de [`RoboCarro.ino`](RoboCarro.ino):

| Constante | Para que serve |
|---|---|
| `INVERTE_MOTOR_A`, `INVERTE_MOTOR_B` | Troque se um motor girar ao contrário |
| `MOTOR_A_ESQUERDO` | `false` se o motor A for o da direita |
| `DIST_PARADA_CM` | Distância em que o carro para e bloqueia a frente (15 cm) |
| `DIST_ALERTA_CM` | Distância em que começam os bipes (40 cm) |
| `AQUECIMENTO_MS` | Tempo de aquecimento do MQ135 antes de calibrar |
| `GAS_DELTA_MEDIO`, `GAS_DELTA_RUIM` | Quanto acima da referência o ar fica médio ou ruim |
| `IR_LIMIAR`, `IR_ATIVO_BAIXO` | Sensibilidade e lógica do sensor de chama |
| `VEL_PWM_MIN` | PWM mínimo que faz os motores girarem |
| `TIMEOUT_COMANDO_MS` | Parada de segurança do controle pelo PC (`0` desliga) |
| `BT_BAUD` | Velocidade da serial do módulo Bluetooth (9600) |
| `BT_USA_STATE` | `true` para parar quando o Bluetooth desconectar. Requer o pino STATE do HC-05 ligado no A6 |

Enquanto os motores andam, e por 3 s depois, a leitura do ar fica congelada no último valor, porque a corrente dos motores mexe na leitura. O mesmo acontece quando a alimentação troca entre USB e bateria: a leitura congela até a tensão estabilizar.

## Problemas comuns

**"Connection failed: read failed, socket might closed or timeout" no celular**
- Veja se outro app ainda está conectado no módulo. Ele aceita uma conexão só; force a parada do outro app.
- Desfaça o pareamento e pareie de novo.
- Confira se o VCC do módulo está no 5V.

**O celular conecta (LED fixo), mas o robô não obedece**
- O código avisa na serial ao receber **qualquer** byte do Bluetooth. Se "Celular conectado (Dabble)" não aparece, o sinal não está chegando ao D2.
- Confira se o TXD do módulo vai no D2 e o RXD no D4.
- Com um multímetro, meça o D2 em relação ao GND:
  - ~3,3 V: o D2 está ligado ao módulo;
  - ~5 V: o fio não está no TXD.

**O app "Arduino Bluetooth Control" conecta, mas o robô não responde**
- Esse firmware entende só o protocolo do Dabble. Apps que mandam letras não funcionam pelo Bluetooth.

**Os motores não giram ou só um lado anda**
- Tire os jumpers ENA/ENB.
- Ligue os GNDs juntos.
- Confira a bateria dos motores.
- Se o carro anda torto ou gira ao contrário, ajuste `INVERTE_MOTOR_*` e `MOTOR_A_ESQUERDO`.

**`dist=-1` o tempo todo**
- O HC-SR04 não está recebendo eco: confira TRIG (D5), ECHO (D12) e a alimentação de 5 V.
- Também acontece quando não há nada a menos de 4 m na frente.

**"LCD I2C nao encontrado"**
- Confira SDA no A4, SCL no A5 e a alimentação do módulo I2C.
