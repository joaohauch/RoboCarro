"""Controle do RoboCarro pelo teclado (Windows).

Uso: python controle.py [PORTA]   (padrao COM6)
Segure setas/WASD para andar (soltou, parou). 1-9 velocidade, m mudo,
c calibra ar, t liga/desliga telemetria, h ajuda, q/Esc sai.
"""
import ctypes
import msvcrt
import sys
import threading
import time

import serial

PORTA = sys.argv[1] if len(sys.argv) > 1 else "COM6"
INTERVALO = 0.1  # reenvio enquanto segura (o Arduino para apos 400 ms sem comando)

user32 = ctypes.windll.user32
# comando -> teclas virtuais (letra e seta)
MOVIMENTO = {"w": (0x57, 0x26), "s": (0x53, 0x28), "a": (0x41, 0x25), "d": (0x44, 0x27)}
UNICAS = set("123456789mcth")


def leitor(ser):
    while ser.is_open:
        try:
            linha = ser.readline()
        except serial.SerialException:
            break
        if linha:
            print(linha.decode(errors="replace").rstrip())


def tecla_movimento(janela):
    # So aceita teclas com esta janela em foco, para nao dirigir digitando em outro lugar.
    if user32.GetForegroundWindow() != janela:
        return "x"
    for cmd, vks in MOVIMENTO.items():
        if any(user32.GetAsyncKeyState(vk) & 0x8000 for vk in vks):
            return cmd
    return "x"


def main():
    ser = serial.Serial(PORTA, 115200, timeout=0.2)
    janela = user32.GetForegroundWindow()
    print(f"Conectado em {PORTA} (o Arduino reinicia ao conectar).")
    print(__doc__.split("\n", 3)[3])
    threading.Thread(target=leitor, args=(ser,), daemon=True).start()

    enviado, ultimo_envio = "x", 0.0
    try:
        while True:
            while msvcrt.kbhit():  # teclas de toque unico (e descarta o resto)
                k = msvcrt.getwch()
                if k in ("\x00", "\xe0"):
                    msvcrt.getwch()
                    continue
                k = k.lower()
                if k in ("q", "\x1b", "\x03"):
                    return
                if k in UNICAS:
                    ser.write(k.encode())

            cmd = tecla_movimento(janela)
            agora = time.monotonic()
            if cmd != enviado or (cmd != "x" and agora - ultimo_envio >= INTERVALO):
                ser.write(cmd.encode())
                enviado, ultimo_envio = cmd, agora
            time.sleep(0.02)
    except KeyboardInterrupt:
        pass
    finally:
        ser.write(b"x")
        time.sleep(0.1)
        ser.close()


if __name__ == "__main__":
    main()
