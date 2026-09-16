#!/usr/bin/env python3

import glob
import os
import select
import sys
import termios
import tty


BAUD_RATE = 115200
PORT_PATTERNS = (
    "/dev/cu.usbmodem*",
    "/dev/cu.usbserial*",
    "/dev/cu.SLAB_USBtoUART*",
    "/dev/cu.wchusbserial*",
)


def choose_serial_port(argument: str | None) -> str:
    if argument:
        if not os.path.exists(argument):
            raise RuntimeError(f"Serial port does not exist: {argument}")
        return argument

    ports = sorted({port for pattern in PORT_PATTERNS for port in glob.glob(pattern)})
    if not ports:
        raise RuntimeError("No serial ports found. Connect the board and try again.")

    print("Select serial port:")
    for index, port in enumerate(ports, start=1):
        print(f"  {index}) {port}")
    default = " [1]" if len(ports) == 1 else ""
    selection = input(f"Port number{default}: ").strip()
    if not selection and len(ports) == 1:
        selection = "1"
    if not selection.isdecimal() or not 1 <= int(selection) <= len(ports):
        raise RuntimeError("Invalid port selection.")
    return ports[int(selection) - 1]


def configure_serial(serial_fd: int) -> None:
    attributes = termios.tcgetattr(serial_fd)
    attributes[0] = 0
    attributes[1] = 0
    attributes[2] &= ~(termios.CSIZE | termios.PARENB | termios.CSTOPB)
    attributes[2] |= termios.CS8 | termios.CLOCAL | termios.CREAD
    attributes[3] = 0
    attributes[4] = termios.B115200
    attributes[5] = termios.B115200
    attributes[6][termios.VMIN] = 0
    attributes[6][termios.VTIME] = 1
    termios.tcsetattr(serial_fd, termios.TCSANOW, attributes)


def read_escape_sequence(input_fd: int) -> bytes | None:
    ready, _, _ = select.select([input_fd], [], [], 0.15)
    if not ready:
        return None
    prefix = os.read(input_fd, 1)
    if prefix not in (b"[", b"O"):
        return None
    ready, _, _ = select.select([input_fd], [], [], 0.15)
    return os.read(input_fd, 1) if ready else b""


def send_command(serial_fd: int, command: str) -> None:
    os.write(serial_fd, f"{command}\n".encode("ascii"))
    print(f"\rSent: {command}")


def run_controller(serial_port: str) -> None:
    input_fd = sys.stdin.fileno()
    original_terminal = termios.tcgetattr(input_fd)
    serial_fd = os.open(serial_port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        configure_serial(serial_fd)
        tty.setraw(input_fd)
        print("\033]0;Slide Player Control\007", end="")
        print(f"Connected to {serial_port} at {BAUD_RATE} baud.")
        print("Left=last, Right=next, number+Enter=open slide, Esc=quit.")

        number_buffer = ""
        while True:
            ready, _, _ = select.select([input_fd, serial_fd], [], [])
            if serial_fd in ready:
                output = os.read(serial_fd, 4096)
                if output:
                    sys.stdout.buffer.write(output)
                    sys.stdout.buffer.flush()
            if input_fd not in ready:
                continue

            key = os.read(input_fd, 1)
            if key in (b"\x03", b"\x1b"):
                if key == b"\x03":
                    break
                direction = read_escape_sequence(input_fd)
                if direction is None:
                    break
                if direction == b"C":
                    send_command(serial_fd, "next")
                elif direction == b"D":
                    send_command(serial_fd, "last")
            elif key in (b"\r", b"\n"):
                if number_buffer:
                    send_command(serial_fd, number_buffer)
                    number_buffer = ""
            elif key in (b"\x08", b"\x7f"):
                number_buffer = number_buffer[:-1]
                print(f"\rSlide: {number_buffer:<10}", end="", flush=True)
            elif b"0" <= key <= b"9":
                number_buffer += key.decode("ascii")
                print(f"\rSlide: {number_buffer:<10}", end="", flush=True)
    finally:
        termios.tcsetattr(input_fd, termios.TCSADRAIN, original_terminal)
        os.close(serial_fd)
        print("\nDisconnected.")


def main() -> int:
    if not sys.stdin.isatty():
        print("Slide Player Control must run in a terminal.", file=sys.stderr)
        return 1
    try:
        serial_port = choose_serial_port(sys.argv[1] if len(sys.argv) > 1 else None)
        run_controller(serial_port)
    except (OSError, RuntimeError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())