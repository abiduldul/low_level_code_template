#!/usr/bin/env python3
import sys
import serial
import threading
import time

# Configuration
VIRTUAL_PORT = '/tmp/esp32'
BAUDRATE = 115200  # Note: Baudrate is ignored by virtual PTYs, but required by API

def receive_thread(ser):
    """Continuously listens for data from the STM32/ESP32 and prints it."""
    print("[*] Receiver thread active.")
    while True:
        try:
            # Block until at least 1 byte is available
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                # Decode bytes to text, replacing unprintable characters safely
                sys.stdout.write(data.decode('utf-8', errors='replace'))
                sys.stdout.flush()
            else:
                time.sleep(0.01) # Small sleep to prevent high CPU utilization
        except serial.SerialException as e:
            print(f"\n[!] Serial read error: {e}")
            break
        except Exception as e:
            print(f"\n[!] Unexpected read error: {e}")
            break

def main():
    print(f"Connecting to virtual port: {VIRTUAL_PORT}...")
    try:
        # Open the virtual serial port
        ser = serial.Serial(VIRTUAL_PORT, BAUDRATE, timeout=1.0)
    except serial.SerialException as e:
        print(f"[!] Could not open port {VIRTUAL_PORT}: {e}")
        print("[!] Make sure your multiplexer script is running!")
        sys.exit(1)

    print(f"Connected! Connected to {VIRTUAL_PORT}")
    print("Type your commands below and press ENTER to send.")
    print("Press Ctrl+C to exit.\n" + "-"*50)

    # Start the background receiver thread
    rx_thread = threading.Thread(target=receive_thread, args=(ser,), daemon=True)
    rx_thread.start()

    # Main thread handles user transmission loop
    try:
        while True:
            # Read line from keyboard input
            user_input = input()
            
            # Append Carriage Return + New Line (Standard for AT commands/REPLs)
            # Change this to just b'\n' or b'\r' if your ESP32 application expects something else
            packet = user_input.encode('utf-8') + b'\r\n'
            
            # Send it out to the PTY master -> Multiplexer -> STM32 -> ESP32
            ser.write(packet)
            ser.flush()
            
    except KeyboardInterrupt:
        print("\nExiting virtual terminal client. Goodbye!")
    finally:
        ser.close()

if __name__ == '__main__':
    main()