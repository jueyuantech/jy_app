#!/usr/bin/env python3
"""Inject simulator OS events through the FIFO or Windows named pipe."""

import datetime as dt
import os
import tkinter as tk
from tkinter import ttk

FIFO_PATH_DEFAULT = r"\\.\pipe\floatair_sim_event_fifo" if os.name == "nt" else "/tmp/floatair_sim_event_fifo"

GROUPS = [
    ("Host", [
        ("Host Connected", "SET_JYP_HOST_CONNECTED"),
        ("Host Disconnected", "SET_JYP_HOST_DISCONNECTED"),
        ("TWS Broken", "SET_TWS_LINK_BROKEN"),
        ("KWS Hit", "SET_KWS_HIT"),
    ]),
    ("Battery", [
        ("Low Battery", "SET_JYT_LOW_BATTERY_WARNING"),
        ("Charging", "SET_CHARGER_ON"),
        ("Not Charging", "SET_CHARGER_OFF"),
    ]),
    ("Wear", [
        ("Wear On", "SET_IED_WEAR_ON"),
        ("Removed", "SET_IED_REMOVED"),
    ]),
    ("Clicks", [
        ("Slide Backward", "SET_SLIDE_BACKWORD"),
        ("Single Click", "SET_FORCE_SINGLE_CLICK"),
        ("Double Click", "SET_FORCE_DOUBLE_CLICK"),
        ("Triple Click", "SET_FORCE_TRI_CLICK"),
        ("Long Press", "SET_FORCE_LONG_PRESSED"),
        ("Slide Forward", "SET_SLIDE_FORWARD"),
    ]),
    ("IMU", [
        ("IMU Single Tap", "SET_IMU_SINGLE_TAP"),
        ("IMU Double Tap", "SET_IMU_DOUBLE_TAP"),
        ("Head Up", "SET_IMU_TILT_UP"),
        ("Head Down", "SET_IMU_TILT_DOWN"),
    ]),
]


class EventPanel:
    def __init__(self, fifo_path: str) -> None:
        self.fifo_path = fifo_path
        self.root = tk.Tk()
        self.root.title("Floatair OS Events")
        self.root.geometry("760x760")
        self.root.resizable(False, False)
        self.status_var = tk.StringVar(value=f"FIFO: {fifo_path}")
        self.battery_soc_value = tk.StringVar(value="80")
        self.time_text_value = tk.StringVar(value=dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
        self.caller_value = tk.StringVar(value="10086")

        style = ttk.Style()
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass

        container = ttk.Frame(self.root, padding=12)
        container.pack(fill="both", expand=True)
        container.columnconfigure(0, weight=1)

        title = ttk.Label(container, text="OS Event Pad")
        title.pack(anchor="w", pady=(0, 8))

        groups_frame = ttk.Frame(container)
        groups_frame.pack(fill="x", expand=False)
        groups_frame.columnconfigure(0, weight=1)

        for row, (group_name, buttons) in enumerate(GROUPS):
            frame = ttk.LabelFrame(groups_frame, text=group_name, padding=8)
            frame.grid(row=row, column=0, pady=(0 if row == 0 else 8, 0), sticky="ew")
            frame.columnconfigure(tuple(range(len(buttons))), weight=1)

            for col, (label, event_name) in enumerate(buttons):
                btn = ttk.Button(frame, text=label, command=lambda name=event_name: self.send_event(name))
                btn.grid(row=0, column=col, sticky="ew", padx=(0 if col == 0 else 6, 0))
            if group_name == "Battery":
                soc_frame = ttk.Frame(frame)
                soc_frame.grid(row=1, column=0, columnspan=len(buttons), sticky="ew", pady=(10, 0))
                soc_frame.columnconfigure(1, weight=1)

                ttk.Label(soc_frame, text="SOC").grid(row=0, column=0, sticky="w", padx=(0, 8))
                ttk.Entry(soc_frame, textvariable=self.battery_soc_value, width=12).grid(row=0, column=1, sticky="ew")
                ttk.Button(soc_frame, text="Send", command=self.send_battery_soc).grid(row=0, column=2, padx=(8, 0))

                battery_quick_frame = ttk.Frame(frame)
                battery_quick_frame.grid(row=2, column=0, columnspan=len(buttons), sticky="w", pady=(8, 0))
                for col, value in enumerate(("0", "20", "50", "80", "100")):
                    ttk.Button(
                        battery_quick_frame,
                        text=f"{value}%",
                        command=lambda v=value: self.send_battery_soc_quick(v),
                        width=6,
                    ).grid(row=0, column=col, padx=(0 if col == 0 else 6, 0))

        tools_frame = ttk.LabelFrame(container, text="Tools", padding=8)
        tools_frame.pack(fill="x", expand=False, pady=(10, 0))
        tools_frame.columnconfigure(1, weight=1)

        ttk.Label(tools_frame, text="Time Sync").grid(row=0, column=0, sticky="w", padx=(0, 8))
        time_frame = ttk.Frame(tools_frame)
        time_frame.grid(row=0, column=1, sticky="ew")
        time_frame.columnconfigure(1, weight=1)
        ttk.Button(time_frame, text="Sync Now", command=self.send_time_now).grid(row=0, column=0, padx=(0, 8))
        ttk.Entry(time_frame, textvariable=self.time_text_value, width=22).grid(row=0, column=1, sticky="ew")
        ttk.Button(time_frame, text="Send", command=self.send_time_text).grid(row=0, column=2, padx=(8, 0))

        ttk.Label(tools_frame, text="Phone").grid(row=1, column=0, sticky="w", padx=(0, 8), pady=(10, 0))
        phone_frame = ttk.Frame(tools_frame)
        phone_frame.grid(row=1, column=1, sticky="ew", pady=(10, 0))
        phone_frame.columnconfigure(0, weight=1)
        ttk.Entry(phone_frame, textvariable=self.caller_value, width=18).grid(row=0, column=0, sticky="ew")
        ttk.Button(phone_frame, text="Ringing", command=lambda: self.send_call_event("SET_BT_CALL_RINGING")).grid(row=0, column=1, padx=(8, 0))
        ttk.Button(phone_frame, text="Connected", command=lambda: self.send_call_event("SET_BT_CALL_CONNECTED")).grid(row=0, column=2, padx=(8, 0))
        ttk.Button(phone_frame, text="Disconnected", command=lambda: self.send_call_event("SET_BT_CALL_DISCONNECTED")).grid(row=0, column=3, padx=(8, 0))

        status = ttk.Label(container, textvariable=self.status_var, foreground="#445")
        status.pack(anchor="w", pady=(10, 0))

    def _write_line(self, line: str) -> None:
        try:
            flags = os.O_WRONLY
            if hasattr(os, "O_BINARY"):
                flags |= os.O_BINARY
            if os.name != "nt":
                flags |= os.O_NONBLOCK
            fd = os.open(self.fifo_path, flags)
            try:
                os.write(fd, (line + "\n").encode("utf-8"))
            finally:
                os.close(fd)
            self.status_var.set(f"Sent: {line}")
        except FileNotFoundError:
            channel_name = "Named pipe" if os.name == "nt" else "FIFO"
            self.status_var.set(f"{channel_name} not found, please start simulator first")
        except OSError as exc:
            self.status_var.set(f"Send failed: {exc}")

    def send_event(self, event_name: str) -> None:
        self._write_line(event_name)

    def send_event_with_arg(self, event_name: str, arg: str) -> None:
        arg = arg.strip()
        if not arg:
            self.status_var.set(f"Missing arg for {event_name}")
            return
        self._write_line(f"{event_name} {arg}")

    def send_battery_soc(self) -> None:
        soc = self.battery_soc_value.get().strip()
        if not soc:
            self.status_var.set("Battery SOC is required")
            return
        self._write_line(f"SET_BAT_SOC {soc}")

    def send_battery_soc_quick(self, soc: str) -> None:
        self.battery_soc_value.set(soc)
        self.send_battery_soc()

    def send_time_now(self) -> None:
        self._write_line("SET_REPORT_DEVICE_STATE_NOW")

    def send_time_text(self) -> None:
        time_text = self.time_text_value.get().strip()
        if not time_text:
            self.status_var.set("Time text is required")
            return
        try:
            parsed = dt.datetime.strptime(time_text, "%Y-%m-%d %H:%M:%S")
        except ValueError:
            self.status_var.set("Time format: YYYY-MM-DD HH:MM:SS")
            return
        epoch = int(parsed.timestamp())
        self._write_line(f"SET_REPORT_DEVICE_STATE {epoch}")

    def send_call_event(self, event_name: str) -> None:
        caller = self.caller_value.get().strip()
        if caller:
            self._write_line(f"{event_name} {caller}")
        else:
            self._write_line(event_name)

    def run(self) -> None:
        self.root.mainloop()


def main() -> None:
    EventPanel(FIFO_PATH_DEFAULT).run()


if __name__ == "__main__":
    main()
