#!/usr/bin/env python3
"""Inject simulator OS events through the FIFO or Windows named pipe."""

import ctypes as ct
from ctypes import wintypes as wt
import datetime as dt
import os
import sys
import tkinter as tk
from tkinter import ttk

FIFO_PATH_DEFAULT = r"\\.\pipe\floatair_sim_event_fifo" if os.name == "nt" else "/tmp/floatair_sim_event_fifo"

# Apple 自带的 Tk 8.5 在较新的 macOS 上只会画出空白窗口，控件全部不可见。
MIN_TK_VERSION = 8.6

GROUPS = [
    ("Host", [
        ("Host Connected", "SET_JYP_HOST_CONNECTED"),
        ("Host Disconnected", "SET_JYP_HOST_DISCONNECTED"),
        ("TWS Broken", "SET_TWS_LINK_BROKEN"),
        ("KWS Hit (Configured)", "SET_KWS_HIT"),
    ]),
    ("Battery", []),
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
        ("Slide Forward", "SET_SLIDE_FORWARD")
    ]),
    ("IMU", [
        ("IMU Single Tap", "SET_IMU_SINGLE_TAP"),
        ("IMU Double Tap", "SET_IMU_DOUBLE_TAP"),
        ("Any Motion", "SET_IMU_ANY_MOTION"),
        ("No Motion", "SET_IMU_NO_MOTION"),
        ("Head Up", "SET_IMU_TILT_UP"),
        ("Head Down", "SET_IMU_TILT_DOWN"),
    ]),
]

ATTACHMENT_TYPES = [  # 与 JYT_ATTACHMENT_TYPE 的协议值保持一致。
    ("Glasses Case", 1),
    ("Charging Cable", 2),
    ("Headset", 3),
    ("Empty", 0),
]

ATTACHMENT_SIDES = [  # 当前硬件定义：从侧为左侧，主侧为右侧。
    ("Left", 1),
    ("Right", 0),
]

BATTERY_SIDES = [  # 顺序对应 OS 电池侧别：0 主侧/右腿，1 从侧/左腿。
    ("Master / Right", "SET_BAT_STATUS"),
    ("Slave / Left", "SET_BAT_STATUS_SLAVE"),
]


class TitlebarTopmostMenu:
    """保留系统标题栏和窗口过程，不创建额外的按钮或浮层。"""

    COMMAND_ID = 0x1F10  # 自定义系统菜单命令：低四位为零，且小于 0xF000。

    def __init__(self, root):
        self.root = root
        self.user32 = ct.WinDLL("user32", use_last_error=True)
        self.proc_type = ct.WINFUNCTYPE(ct.c_ssize_t, wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM)
        signatures = {
            "GetParent": ([wt.HWND], wt.HWND),
            "GetSystemMenu": ([wt.HWND, wt.BOOL], wt.HMENU),
            "InsertMenuW": ([wt.HMENU, wt.UINT, wt.UINT, ct.c_size_t, wt.LPCWSTR], wt.BOOL),
            "CheckMenuItem": ([wt.HMENU, wt.UINT, wt.UINT], wt.DWORD),
            "CallWindowProcW": ([ct.c_void_p, wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM], ct.c_ssize_t),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.user32, name)
            fn.argtypes, fn.restype = args, result
        suffix = "PtrW" if ct.sizeof(ct.c_void_p) == 8 else "W"
        self.get_long = getattr(self.user32, "GetWindowLong" + suffix)
        self.set_long = getattr(self.user32, "SetWindowLong" + suffix)
        self.get_long.argtypes, self.get_long.restype = [wt.HWND, ct.c_int], ct.c_ssize_t
        self.set_long.argtypes, self.set_long.restype = [wt.HWND, ct.c_int, ct.c_ssize_t], ct.c_ssize_t

        root.update_idletasks()
        self.hwnd = self.user32.GetParent(root.winfo_id())
        self.menu = self.user32.GetSystemMenu(self.hwnd, False)
        if not self.menu:
            raise ct.WinError(ct.get_last_error())
        # 放在“关闭”前；勾选标记由 Windows 原生菜单绘制。
        if not self.user32.InsertMenuW(self.menu, 0xF060, 0, self.COMMAND_ID, "置顶"):
            raise ct.WinError(ct.get_last_error())
        self.user32.InsertMenuW(self.menu, 0xF060, 0x0800, 0, None)  # MF_SEPARATOR
        self._sync_checked()

        self.original = self.get_long(self.hwnd, -4)
        self.callback = self.proc_type(self._window_proc)
        if not self.set_long(self.hwnd, -4, ct.cast(self.callback, ct.c_void_p).value):
            raise ct.WinError(ct.get_last_error())

    def _sync_checked(self):
        flags = 0x0008 if bool(self.root.attributes("-topmost")) else 0
        self.user32.CheckMenuItem(self.menu, self.COMMAND_ID, flags)

    def _window_proc(self, hwnd, message, wparam, lparam):
        if message == 0x0112 and (wparam & 0xFFF0) == self.COMMAND_ID:  # WM_SYSCOMMAND
            self.root.attributes("-topmost", not bool(self.root.attributes("-topmost")))
            self._sync_checked()
            return 0
        if message == 0x0117 and wparam == self.menu:  # WM_INITMENUPOPUP
            self._sync_checked()
        if message == 0x0082:  # WM_NCDESTROY
            self.set_long(hwnd, -4, self.original)
        return self.user32.CallWindowProcW(self.original, hwnd, message, wparam, lparam)


class EventPanel:
    def __init__(self, fifo_path: str) -> None:
        self.fifo_path = fifo_path
        self.root = tk.Tk()
        self.root.title("Floatair OS Events")
        self.root.geometry("760x760")
        self.root.resizable(True, True)
        self.status_var = tk.StringVar(value=f"FIFO: {fifo_path}")
        self.battery_side = tk.IntVar(value=0)  # 默认右腿/主侧，切换侧别不发送事件。
        self.battery_soc_values = [tk.StringVar(value="80") for _ in BATTERY_SIDES]
        self.battery_charge_modes = [0 for _ in BATTERY_SIDES]  # 两侧独立缓存：0 未充电，1 充电。
        self.case_battery_soc_value = tk.StringVar(value="80")
        self.time_text_value = tk.StringVar(value=dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
        self.caller_value = tk.StringVar(value="10086")

        style = ttk.Style()
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass

        self.root.attributes("-topmost", True)
        if os.name == "nt":
            self.titlebar_menu = TitlebarTopmostMenu(self.root)
        else:
            self.always_on_top = tk.BooleanVar(value=True)
            toolbar = ttk.Frame(self.root, padding=(12, 8))
            toolbar.pack(fill="x")
            ttk.Checkbutton(
                toolbar, text="钉在最前", variable=self.always_on_top,
                command=lambda: self.root.attributes("-topmost", self.always_on_top.get()),
                style="Toolbutton",
            ).pack(side="right")

        viewport = ttk.Frame(self.root)
        viewport.pack(fill="both", expand=True)

        canvas = tk.Canvas(
            viewport,
            borderwidth=0,
            highlightthickness=0,
            background=self.root.cget("background"),
        )
        scrollbar = ttk.Scrollbar(viewport, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=scrollbar.set)
        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        container = ttk.Frame(canvas, padding=12)
        container_window = canvas.create_window((0, 0), window=container, anchor="nw")
        container.bind(
            "<Configure>",
            lambda _event: canvas.configure(scrollregion=canvas.bbox("all")),
        )
        canvas.bind(
            "<Configure>",
            lambda event: canvas.itemconfigure(container_window, width=event.width),
        )
        self.root.bind(
            "<MouseWheel>",
            lambda event: canvas.yview_scroll(-1 if event.delta > 0 else 1, "units"),
        )
        self.root.bind("<Button-4>", lambda _event: canvas.yview_scroll(-1, "units"))
        self.root.bind("<Button-5>", lambda _event: canvas.yview_scroll(1, "units"))
        container.columnconfigure(0, weight=1)

        title = ttk.Label(container, text="OS Event Pad")
        title.pack(anchor="w", pady=(0, 8))

        groups_frame = ttk.Frame(container)
        groups_frame.pack(fill="x", expand=False)
        groups_frame.columnconfigure(0, weight=1)

        for row, (group_name, buttons) in enumerate(GROUPS):
            frame = ttk.LabelFrame(groups_frame, text=group_name, padding=8)
            frame.grid(row=row, column=0, pady=(0 if row == 0 else 8, 0), sticky="ew")
            frame.columnconfigure(tuple(range(max(1, len(buttons)))), weight=1)

            for col, (label, event_name) in enumerate(buttons):
                btn = ttk.Button(frame, text=label, command=lambda name=event_name: self.send_event(name))
                btn.grid(row=0, column=col, sticky="ew", padx=(0 if col == 0 else 6, 0))
            if group_name == "Battery":
                actions = ttk.Frame(frame)
                actions.grid(row=0, column=0, sticky="ew")
                actions.columnconfigure((0, 1, 2), weight=1)
                ttk.Button(actions, text="Low Battery", command=lambda: self._write_line("SET_JYT_LOW_BATTERY_WARNING")).grid(
                    row=0, column=0, sticky="ew"
                )
                ttk.Button(actions, text="charger", command=lambda: self.send_battery_status(charge_mode=1)).grid(
                    row=0, column=1, sticky="ew", padx=(6, 0)
                )
                ttk.Button(actions, text="nocharger", command=lambda: self.send_battery_status(charge_mode=0)).grid(
                    row=0, column=2, sticky="ew", padx=(6, 0)
                )

                soc_frame = ttk.Frame(frame)
                soc_frame.grid(row=2, column=0, sticky="ew", pady=(8, 0))
                soc_frame.columnconfigure(1, weight=1)
                ttk.Label(soc_frame, text="SOC").grid(row=0, column=0, sticky="w", padx=(0, 8))
                soc_entry = ttk.Entry(soc_frame, textvariable=self.battery_soc_values[0], width=12)
                soc_entry.grid(row=0, column=1, sticky="ew")
                ttk.Button(soc_frame, text="Send", command=self.send_battery_status).grid(row=0, column=2, padx=(8, 0))

                side_frame = ttk.Frame(frame)
                side_frame.grid(row=1, column=0, sticky="w", pady=(8, 0))
                ttk.Label(side_frame, text="Side").grid(row=0, column=0, padx=(0, 8))
                for col, (label, side) in enumerate(ATTACHMENT_SIDES, start=1):
                    ttk.Radiobutton(
                        side_frame, text=label, value=side, variable=self.battery_side,
                        command=lambda s=side, entry=soc_entry: entry.configure(textvariable=self.battery_soc_values[s]),
                    ).grid(row=0, column=col, padx=(0, 8))

                quick_frame = ttk.Frame(frame)
                quick_frame.grid(row=3, column=0, sticky="w", pady=(8, 0))
                for col, value in enumerate(("0", "20", "50", "80", "100")):
                    ttk.Button(
                        quick_frame, text=f"{value}%", width=6,
                        command=lambda v=value: self.send_battery_status(soc=v),
                    ).grid(row=0, column=col, padx=(0 if col == 0 else 6, 0))

                case_soc_frame = ttk.Frame(frame)
                case_soc_frame.grid(row=4, column=0, sticky="ew", pady=(8, 0))
                case_soc_frame.columnconfigure(1, weight=1)
                ttk.Label(case_soc_frame, text="Case SOC").grid(row=0, column=0, sticky="w", padx=(0, 8))
                ttk.Entry(case_soc_frame, textvariable=self.case_battery_soc_value, width=12).grid(
                    row=0, column=1, sticky="ew"
                )
                ttk.Button(case_soc_frame, text="Send", command=self.send_case_battery_soc).grid(
                    row=0, column=2, padx=(8, 0)
                )

        attachment_frame = ttk.LabelFrame(groups_frame, text="Attachments", padding=8)
        attachment_frame.grid(row=len(GROUPS), column=0, pady=(8, 0), sticky="ew")
        for col in range(1, len(ATTACHMENT_TYPES) + 1):
            attachment_frame.columnconfigure(col, weight=1)

        for row, (side_label, side_value) in enumerate(ATTACHMENT_SIDES):
            ttk.Label(attachment_frame, text=side_label, width=8).grid(
                row=row,
                column=0,
                sticky="w",
                pady=(0 if row == 0 else 8, 0),
            )
            for col, (type_label, type_value) in enumerate(ATTACHMENT_TYPES, start=1):
                event_line = f"SET_JYT_ACC_TYPE_CHANGED {type_value} {side_value}"
                ttk.Button(
                    attachment_frame,
                    text=type_label,
                    command=lambda line=event_line: self._write_line(line),
                ).grid(
                    row=row,
                    column=col,
                    sticky="ew",
                    padx=(8, 0),
                    pady=(0 if row == 0 else 8, 0),
                )

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
        ttk.Button(phone_frame, text="Outgoing", command=lambda: self.send_call_event("SET_BT_CALL_OUTGOING")).grid(row=0, column=2, padx=(8, 0))
        ttk.Button(phone_frame, text="Connected", command=lambda: self.send_call_event("SET_BT_CALL_CONNECTED")).grid(row=0, column=3, padx=(8, 0))
        ttk.Button(phone_frame, text="Disconnected", command=lambda: self.send_call_event("SET_BT_CALL_DISCONNECTED")).grid(row=0, column=4, padx=(8, 0))

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

    def send_battery_status(self, soc: str = None, charge_mode: int = None) -> None:
        """校验并发送当前选中侧的电池快照，不修改另一侧的缓存。

        参数 soc：快捷电量，省略时读取输入框；charge_mode：0/1，省略时沿用该侧缓存。返回值：无。
        """
        side = self.battery_side.get()
        label, event = BATTERY_SIDES[side]
        try:
            soc = int((self.battery_soc_values[side].get() if soc is None else soc).strip())
            if not 0 <= soc <= 100:
                raise ValueError
        except ValueError:
            self.status_var.set(f"{label}: SOC must be an integer from 0 to 100")
            return
        self.battery_soc_values[side].set(str(soc))
        if charge_mode is not None:
            self.battery_charge_modes[side] = charge_mode
        self._write_line(f"{event} {soc} {self.battery_charge_modes[side]}")

    def send_case_battery_soc(self) -> None:
        try:
            soc = int(self.case_battery_soc_value.get().strip())
            if not 0 <= soc <= 100:
                raise ValueError
        except ValueError:
            self.status_var.set("Case battery SOC must be an integer from 0 to 100")
            return
        self._write_line(f"SET_CASE_SOC_REPORT {soc}")

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


def check_tk_version() -> None:
    if tk.TkVersion >= MIN_TK_VERSION:
        return
    print(
        f"[ERROR] Tk {tk.TkVersion} is too old and would show a blank window.\n"
        f"        Use a Python with Tk >= {MIN_TK_VERSION}, e.g. /opt/homebrew/bin/python3.14.",
        file=sys.stderr,
    )
    raise SystemExit(1)


def main() -> None:
    check_tk_version()
    EventPanel(FIFO_PATH_DEFAULT).run()


if __name__ == "__main__":
    main()
