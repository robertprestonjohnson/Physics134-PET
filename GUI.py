# This Program creates a Graphical User Interface that sends commands over a serial connection to the RedPitaya
# Students should interact with the program rather than logging into the Red Pitaya over the ethernet.
import threading
import time
import tkinter as tk
from tkinter import ttk

import serial
try:
    from serial.tools import list_ports
except Exception:
    list_ports = None

from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

import os
import re

# UART parameters for the communication between PC and Red Pitaya. These must match what is set in PET_RP_Slave.cpp
SERIAL_CFG = {
    "baudrate": 115200,                   # bits per second for the UART connection. This absolutely must match the value in PET_RP_Slave.cpp
    "timeout": 20,                        # command timeout in seconds. This should be plenty of time for the Red Pitaya to respond
    "handshake_send": b"R U THERE?",      # message sent prior to sending a new command, to see if PET_RP_Slave is executing.
    "handshake_expect": b"YES I AM\n",    # the expected reply from PET_RP_Slave
    "done_token": b"DONE\n",              # message sent by PET_RP_Slave when it has finished executing a command
}

# Default values for the parameters that can be set by the GUI. A few no longer appear in the GUI, for safety (e.g. "write").
DEFAULTS = {
    "angle": "0",
    "nsteps": "1",
    "x0": "5",
    "stepsize": "5",
    "dwelltime": "600",
    "filename": "scanData.csv",
    "number": "1000",
    "time": "0",
    "trgtype": "external",
    "datafile": "coincidences.csv",
    "trigchan": "chA",
    "newpeds": "yes",
    "mxwrite": "0",
    "trglev": "0.2",
    "trghyst": "0",
    "coincwindow": "1",
    "pulsethresh": "10",
    "pedestralA": "0.03898",
    "mean": "45.596",
    "sigma": "2.14",
    "timingA": "47",
    "timingB": "47",
    "dac": "1",
    "mvolts": "20",
    "write": "no",
    "direction": "left",
    "distance": "5",
}

# Define parameters that appear in the list on the left in the GUI and take numerical or string values
NUM_PARAMS = [
    {"key": "nsteps",       "label": "Steps",        "prefix": "-N", "dtype": "int"},
    {"key": "x0",           "label": "X0",           "prefix": "-0", "dtype": "float"},
    {"key": "stepsize",     "label": "Step Size",    "prefix": "-X", "dtype": "float"},
    {"key": "dwelltime",    "label": "Dwell Time",   "prefix": "-W", "dtype": "float"},
    {"key": "number",       "label": "Number",       "prefix": "-n", "dtype": "int"},
    {"key": "time",         "label": "Time",         "prefix": "-T", "dtype": "float"},

    {"key": "mxwrite",      "label": "Mx Write",     "prefix": "-z", "dtype": "int"},
    {"key": "trglev",       "label": "Trglev",       "prefix": "-e", "dtype": "float"},
    {"key": "trghyst",      "label": "Trghyst",      "prefix": "-H", "dtype": "float"},
    {"key": "coincwindow",  "label": "Coinc Window", "prefix": "-i", "dtype": "int"},
    {"key": "pulsethresh",  "label": "Pulse Thresh", "prefix": "-r", "dtype": "float"},
    {"key": "pedestralA",   "label": "Pedestral",    "prefix": "-A", "dtype": "float"},

    {"key": "mean",         "label": "Mean",         "prefix": "-M", "dtype": "float"},
    {"key": "sigma",        "label": "Sigma",        "prefix": "-S", "dtype": "float"},
    {"key": "timingA",      "label": "Timing A",     "prefix": "-a", "dtype": "int"},
    {"key": "timingB",      "label": "Timing B",     "prefix": "-b", "dtype": "int"},
    {"key": "dac",          "label": "Dac",          "prefix": "-D", "dtype": "int"},
    {"key": "mvolts",       "label": "mvolts",       "prefix": "-v", "dtype": "int"},
    {"key": "distance",     "label": "Distance",     "prefix": "-x", "dtype": "float"},
    {"key": "datafile",     "label": "Datafile",     "prefix": "-f", "dtype": "str"},
]

# These two paramters are treated separately in the code from those above, although it isn't clear why. . .
CTRL_ANGLE = {"key": "angle", "label": "Angle", "prefix": "-G", "dtype": "float"}
CTRL_FILE  = {"key": "filename", "label": "File", "prefix": "-F", "dtype": "str"}

# Define parameters that appear in the list on the left in the GUI and have drop-down menus
DROPDOWNS = [
    {"key": "trgtype",   "label": "Trgtype",   "prefix": "-t", "options": ("external", "internal", "pedestal")},
    {"key": "trigchan",  "label": "TrigChan",  "prefix": "-c", "options": ("chA", "chB")},
    {"key": "newpeds",   "label": "Newpeds",   "prefix": "-p", "options": ("yes", "no")},
    {"key": "write",     "label": "Write",     "prefix": "-w", "options": ("yes", "no")},
    {"key": "direction", "label": "Direction", "prefix": "-d", "options": ("left", "right")},
]

# Define the command parameters that each action button will send to the Red Pitaya main program PET_RP_Slave.cpp 
BUTTON_PARAM_SETS = {
    "Scan": [
        "angle", "filename",
        "nsteps", "x0", "stepsize", "dwelltime",
        "number", "time",
        "direction", "distance",
        "trigchan", "datafile", "trgtype", "newpeds",
        "mean", "sigma",
    ],
    "AcquireData": [
        "angle", "filename",
        "number", "time",
        "trigchan", "datafile", "trgtype", "newpeds",
        "mean", "sigma",
    ],
    "moveStage": [
        "nsteps", "x0", "stepsize", "dwelltime",
        "direction", "distance",
    ],
    "setTiming": [
        "timingA", "timingB",
    ],
    "SetDAC": [
        "dac", "mvolts", "write",
    ],
    "Graph": [],
    "Analysis": [],
    "Projection": [],
}

# Define for each action button the name of the corresponding command to be interpreted by PET_RP_Slave.cpp
BUTTON_COMMAND_WORD = {
    "Scan": "scan",
    "AcquireData": "acquireData",
    "moveStage": "moveStage",
    "setTiming": "setTiming",
    "SetDAC": "setDAC",
    "Graph": "graph",
    "Analysis": "analysis",
    "Projection": "projection",
}

INVALID_FILENAME_CHARS = set(r' \/:*?"<>|')

# function to test whether an entered value is really an integer
def is_valid_int_partial(s: str) -> bool:
    if s == "" or s == "-":
        return True
    try:
        int(s)
        return True
    except ValueError:
        return False

# function to test whether an entered value is really floating-point
def is_valid_float_partial(s: str) -> bool:
    if s in ("", "-", ".", "-."):
        return True
    try:
        float(s)
        return True
    except ValueError:
        return False

# function to test whether an entered string can be a valid filename (no bad characters)
def is_valid_filename_partial(s: str) -> bool:
    return all(ch not in INVALID_FILENAME_CHARS for ch in s)

# try to detect valid open COM ports, but the best bet is for the user to enter the correct port into the GUI
def get_detected_ports():
    if list_ports is None:
        return []
    return [p.device for p in list_ports.comports()]


class PETScannerGUI:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("Pet Scanner")
        self.root.minsize(900, 550)

        title_frame = ttk.Frame(root, padding=(10, 8))
        title_frame.grid(row=0, column=0, sticky="ew")
        title_frame.columnconfigure(0, weight=1)
        ttk.Label(title_frame, text="Pet Scanner", font=("Segoe UI", 18, "bold")).grid(row=0, column=0, sticky="w")

        main = ttk.Frame(root)
        main.grid(row=1, column=0, sticky="nsew")
        root.rowconfigure(1, weight=1)
        root.columnconfigure(0, weight=1)

        main.columnconfigure(0, weight=0)
        main.columnconfigure(1, weight=1)
        main.rowconfigure(0, weight=1)

        left_container = ttk.Frame(main)
        left_container.grid(row=0, column=0, sticky="nsw")
        left_container.grid_propagate(False)
        self.left_min_width = 340
        left_container.configure(width=self.left_min_width)

        right = ttk.Frame(main)
        right.grid(row=0, column=1, sticky="nsew")
        right.rowconfigure(0, weight=1)
        right.rowconfigure(1, weight=0)
        right.columnconfigure(0, weight=1)

        self.root.bind("<Configure>", lambda e: self._on_resize(left_container))

        # scrollable left
        self.left_canvas = tk.Canvas(left_container, highlightthickness=0)
        canvas = self.left_canvas
        vsb = ttk.Scrollbar(left_container, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=vsb.set)
        canvas.grid(row=0, column=0, sticky="nsew")
        vsb.grid(row=0, column=1, sticky="ns")
        left_container.rowconfigure(0, weight=1)
        left_container.columnconfigure(0, weight=1)

        self.left_inner = ttk.Frame(canvas, padding=(10, 10))
        inner_id = canvas.create_window((0, 0), window=self.left_inner, anchor="nw")

        def _on_inner_config(_evt):
            canvas.configure(scrollregion=canvas.bbox("all"))
            canvas.itemconfig(inner_id, width=canvas.winfo_width())

        self.left_inner.bind("<Configure>", _on_inner_config)

        # variables
        self.vars = {}

        # Try to get smart about setting the default COM port. If this doesn't work, then the user will have to set the correct port
        # COM list: detected first; fallback COM1..COM7
        detected = get_detected_ports()
        fallback = [f"COM{i}" for i in range(1, 8)]
        self.com_options = detected + [p for p in fallback if p not in detected]

        # default COM selection: try COM5 if exists, else first detected, else COM5
        default_com = "COM5" if "COM5" in self.com_options else (self.com_options[0] if self.com_options else "COM5")
        self.vars["com_port"] = tk.StringVar(value=default_com)

        # control strip vars
        self.vars["angle"] = tk.StringVar(value=DEFAULTS["angle"])
        self.vars["filename"] = tk.StringVar(value=DEFAULTS["filename"])

        # numeric/string left vars
        for p in NUM_PARAMS:
            self.vars[p["key"]] = tk.StringVar(value=DEFAULTS[p["key"]])

        # dropdown vars
        for d in DROPDOWNS:
            self.vars[d["key"]] = tk.StringVar(value=DEFAULTS[d["key"]])

        self.vars["last_angle"] = tk.StringVar(value="—")
        self.vars["plot_status"] = tk.StringVar(value="")

        # validators
        self.vcmd_int = (root.register(self._validate_int), "%P")
        self.vcmd_float = (root.register(self._validate_float), "%P")
        self.vcmd_filename = (root.register(self._validate_filename), "%P")

        # build UI
        self._build_left_panel()
        self._build_graph(right)
        self._build_control_strip(right)

        self.root.after_idle(lambda: self.left_canvas.yview_moveto(0.0))

        # angle clamp
        self.vars["angle"].trace_add("write", self._clamp_angle)

    def _on_resize(self, left_container: ttk.Frame):
        w = self.root.winfo_width()
        target = int(w * 0.25)
        left_w = max(self.left_min_width, target)
        left_container.configure(width=left_w)

    # Function to build the left-hand GUI panel where all the various parameters can be set.
    def _build_left_panel(self):
        # COM selector
        top = ttk.Frame(self.left_inner)
        top.grid(row=0, column=0, columnspan=4, sticky="ew", pady=(0, 10))
        ttk.Label(top, text="COM Port").grid(row=0, column=0, sticky="w", padx=(0, 8))
        com_cb = ttk.Combobox(
            top,
            textvariable=self.vars["com_port"],
            values=self.com_options,
            state="readonly",
            width=12,
        )
        com_cb.grid(row=0, column=1, sticky="w")

        num_by_key = {p["key"]: p for p in NUM_PARAMS}
        dd_by_key = {d["key"]: d for d in DROPDOWNS}

        def make_entry(parent, key: str):
            spec = num_by_key[key]
            dtype = spec["dtype"]
            if dtype == "int":
                vcmd = self.vcmd_int
            elif dtype == "float":
                vcmd = self.vcmd_float
            else:
                vcmd = None
            if vcmd:
                return ttk.Entry(parent, textvariable=self.vars[key], width=14, validate="key", validatecommand=vcmd)
            return ttk.Entry(parent, textvariable=self.vars[key], width=14)

        def make_dropdown(parent, key: str):
            spec = dd_by_key[key]
            return ttk.Combobox(
                parent,
                textvariable=self.vars[key],
                values=list(spec["options"]),
                state="readonly",
                width=12,
            )

        def add_heading(r: int, text: str) -> int:
            ttk.Label(self.left_inner, text=text, font=("Segoe UI", 11, "bold")).grid(
                row=r, column=0, columnspan=4, sticky="w", pady=(8, 2)
            )
            ttk.Separator(self.left_inner, orient="horizontal").grid(
                row=r + 1, column=0, columnspan=4, sticky="ew", pady=(0, 6)
            )
            return r + 2

        def add_row(r: int, left_label, left_key, right_label=None, right_key=None):
            # left pair
            ttk.Label(self.left_inner, text=left_label).grid(row=r, column=0, sticky="w", padx=(0, 6), pady=3)
            if left_key in dd_by_key:
                w1 = make_dropdown(self.left_inner, left_key)
            else:
                w1 = make_entry(self.left_inner, left_key)
            w1.grid(row=r, column=1, sticky="ew", pady=3)

            if right_label is not None and right_key is not None:
                ttk.Label(self.left_inner, text=right_label).grid(row=r, column=2, sticky="w", padx=(12, 6), pady=3)
                if right_key in dd_by_key:
                    w2 = make_dropdown(self.left_inner, right_key)
                else:
                    w2 = make_entry(self.left_inner, right_key)
                w2.grid(row=r, column=3, sticky="ew", pady=3)
            else:
                ttk.Label(self.left_inner, text="").grid(row=r, column=2, sticky="w", pady=3)
                ttk.Label(self.left_inner, text="").grid(row=r, column=3, sticky="w", pady=3)

        r = 1

        # DATA
        r = add_heading(r, "Data")
        add_row(r,   "Number",   "number",  "Time",     "time");     r += 1
        add_row(r,   "Mean",     "mean",    "Sigma",    "sigma");    r += 1
        add_row(r,   "trigchan", "trigchan","trgtype",  "trgtype");  r += 1
        add_row(r,   "newspeds", "newpeds", "datafile", "datafile"); r += 1

        # MOTION
        r = add_heading(r, "Motion")
        add_row(r,   "X0",       "x0",      "distance", "distance"); r += 1
        add_row(r,   "Nsteps",   "nsteps",  "stepsize", "stepsize"); r += 1
        add_row(r,   "dwelltime","dwelltime","direction","direction"); r += 1

        # TIMING
        r = add_heading(r, "Timing")
        add_row(r,   "timingA",  "timingA", "timing B", "timingB");  r += 1

        # DAC
        r = add_heading(r, "Dac")
        add_row(r,   "Dac",      "dac",     "mvolts",   "mvolts");   r += 1
        add_row(r,   "Write",    "write");  r += 1

        for c in range(4):
            self.left_inner.columnconfigure(c, weight=1)

    # Function to build the panel where the graph of counts versus position is displayed.
    def _build_graph(self, right: ttk.Frame):
        graph_frame = ttk.Frame(right, padding=(10, 10))
        graph_frame.grid(row=0, column=0, sticky="nsew")
        graph_frame.rowconfigure(0, weight=1)
        graph_frame.columnconfigure(0, weight=1)

        fig = Figure(figsize=(6, 4))
        self.ax = fig.add_subplot(111)

        self.canvas = FigureCanvasTkAgg(fig, master=graph_frame)
        self.canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")

    # Function to build the row of command "action" buttons below the graph
    def _build_control_strip(self, right: ttk.Frame):
        strip = ttk.Frame(right, padding=(10, 8))
        strip.grid(row=1, column=0, sticky="ew")

        ttk.Label(strip, text="Angle").grid(row=0, column=0, sticky="w", padx=(0, 6))
        ttk.Entry(strip, textvariable=self.vars["angle"], width=10,
                  validate="key", validatecommand=self.vcmd_float).grid(row=0, column=1, sticky="w", padx=(0, 14))

        ttk.Label(strip, text="File").grid(row=0, column=2, sticky="w", padx=(0, 6))
        ttk.Entry(strip, textvariable=self.vars["filename"], width=20,
                  validate="key", validatecommand=self.vcmd_filename).grid(row=0, column=3, sticky="w", padx=(0, 14))

        buttons = ["SetDAC", "setTiming", "moveStage", "AcquireData", "Scan", "Graph", "Analysis", "Projection"]
        col = 4
        for name in buttons:
            ttk.Button(strip, text=name, command=lambda n=name: self.on_button(n)).grid(row=0, column=col, padx=4)
            col += 1

        ttk.Label(strip, text="Plotted angle:").grid(row=1, column=0, sticky="w", pady=(6, 0))
        ttk.Label(strip, textvariable=self.vars["last_angle"]).grid(row=1, column=1, sticky="w", pady=(6, 0))

        ttk.Label(strip, textvariable=self.vars["plot_status"]).grid(row=2, column=0, columnspan=col, sticky="w", pady=(6, 0))

    # functions to validate data entered by the user
    def _validate_int(self, proposed: str) -> bool:
        return is_valid_int_partial(proposed)

    def _validate_float(self, proposed: str) -> bool:
        return is_valid_float_partial(proposed)

    def _validate_filename(self, proposed: str) -> bool:
        return is_valid_filename_partial(proposed)

    def _clamp_angle(self, *_):
        s = self.vars["angle"].get()
        if s in ("", "-", ".", "-."):
            return
        try:
            v = float(s)
        except ValueError:
            return
        if v > 180.0:
            self.vars["angle"].set("180")
        elif v < 0.0:
            self.vars["angle"].set("0")

    # Function to build a command to be sent to PET_RP_Slave on the Red Pitaya
    def build_cmd_list(self, button_name: str):
        cmd_word = BUTTON_COMMAND_WORD.get(button_name, button_name)
        keys_needed = BUTTON_PARAM_SETS.get(button_name, [])

        tokens = ["START", cmd_word]

        if "angle" in keys_needed:
            tokens += [CTRL_ANGLE["prefix"], self._get_token("angle", "float")]
        if "filename" in keys_needed:
            tokens += [CTRL_FILE["prefix"], self._get_token("filename", "str")]

        for p in NUM_PARAMS:
            if p["key"] in keys_needed:
                tokens += [p["prefix"], self._get_token(p["key"], p["dtype"])]

        for d in DROPDOWNS:
            if d["key"] in keys_needed:
                tokens += [d["prefix"], self.vars[d["key"]].get()]

        tokens.append("END")

        data_to_send = " ".join(tokens)
        print(f"[{button_name}] data_to_send: {data_to_send}")
        return tokens

    def _get_token(self, key: str, dtype: str) -> str:
        raw = self.vars[key].get()
        if dtype == "int":
            if raw in ("", "-"):
                return "0"
            try:
                return str(int(raw))
            except ValueError:
                return "0"
        if dtype == "float":
            if raw in ("", "-", ".", "-."):
                return "0"
            try:
                return str(float(raw))
            except ValueError:
                return "0"
        return raw if raw != "" else "0"

    def _set_plot_status(self, msg: str):
        self.root.after(0, lambda: self.vars["plot_status"].set(msg))

    def _try_float(self, v):
        try:
            return float(v)
        except Exception:
            return None

    def _parse_scan_results_text(self, text: str):

        lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
        if not lines:
            raise ValueError("Empty file")

        angle_value = None
        angle_re = re.compile(r"Scan results for angle\s+([+-]?\d+(?:\.\d+)?)")
        m = angle_re.search(lines[0])
        if m:
            angle_value = self._try_float(m.group(1))

        header_idx = None
        for i, ln in enumerate(lines):
            compact = ln.replace(" ", "").lower()
            if compact.startswith("angle,step,position,count"):
                header_idx = i
                break

        if header_idx is None:
            raise ValueError("Could not find header line: 'Angle, Step, Position, Count'")

        positions, counts = [], []
        for ln in lines[header_idx + 1:]:
            parts = [p.strip() for p in ln.split(",")]
            if len(parts) < 4:
                continue
            a = self._try_float(parts[0])
            pos = self._try_float(parts[2])
            cnt = self._try_float(parts[3])

            if angle_value is None and a is not None:
                angle_value = a
            if pos is None or cnt is None:
                continue

            positions.append(pos)
            counts.append(cnt)

        if not positions:
            raise ValueError("No numeric rows found after header")

        return angle_value, positions, counts

    def plot_scan_histogram_from_filename(self):

        path = self.vars["filename"].get().strip()
        if not path:
            self._set_plot_status("Plot error: File box is empty.")
            return

        if not os.path.exists(path):
            self._set_plot_status(f"Plot error: file not found locally: {path}")
            return

        self._set_plot_status(f"Plotting: {path}")

        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                text = f.read()
            ang, pos, cnt = self._parse_scan_results_text(text)
        except Exception as e:
            self._set_plot_status(f"Plot error: {e}")
            return

        self.ax.clear()

        width = 1.0
        if len(pos) >= 2:
            diffs = [abs(pos[i+1] - pos[i]) for i in range(len(pos)-1)]
            diffs = [d for d in diffs if d > 0]
            if diffs:
                width = 0.8 * min(diffs)

        self.ax.bar(pos, cnt, width=width, align="center")
        self.ax.set_xticks(pos)

        if ang is not None:
            self.ax.set_title(f"Count vs Position (Angle = {ang:.6f})")
            self.vars["last_angle"].set(f"{ang:.6f}")
        else:
            self.ax.set_title("Count vs Position (Angle = unknown)")
            self.vars["last_angle"].set("unknown")

        self.ax.set_xlabel("Position")
        self.ax.set_ylabel("Count")

        self.canvas.draw_idle()
        self._set_plot_status(f"Plotted: {os.path.basename(path)}")

    def on_button(self, button_name: str):
        if button_name == "Graph":
            self.root.after(0, self.plot_scan_histogram_from_filename)
            return

        if button_name in ("Analysis", "Projection"):
            print(f"[{button_name}] Empty shell (no action).")
            return

        cmd_list = self.build_cmd_list(button_name)
        port = self.vars["com_port"].get().strip()

        t = threading.Thread(target=self._serial_worker, args=(port, cmd_list), daemon=True)
        t.start()

    # This routine starts each time communication with the PET_RP_Slave code on the Red Pitaya is needed
    def _serial_worker(self, port: str, cmd_list):
        ser = None
        try:
            print(f"[Serial] Opening {port} ...")
            ser = serial.Serial(
                port=port,
                baudrate=SERIAL_CFG["baudrate"],
                stopbits=serial.STOPBITS_ONE,
                parity=serial.PARITY_NONE,
                bytesize=serial.EIGHTBITS,
                timeout=SERIAL_CFG["timeout"],
            )
            time.sleep(1)

            # Send a simple message to the Red Pitaya
            print(f"[Serial] Sending {SERIAL_CFG['handshake_send']!r}")
            ser.write(SERIAL_CFG["handshake_send"])
            reply = ser.readline()
            print(f"[Serial] Reply is {reply!r}")

            # Check that the expected reply is returned by the Red Pitaya. If not, then PET_RP_Slave.cpp probably isn't executing.
            if reply != SERIAL_CFG["handshake_expect"]:
                print("Handshake Protocol Failed; Closing Com Port")
                return

            for token in cmd_list:
                b = token.encode("ascii")
                print(f"[Serial] Sending {b!r}")
                ser.write(b)
                s = ser.readline()
                print(f"[Serial] Reply is {s!r}")

            while True:
                s = ser.readline()
                print(f"[Serial] Message is {s!r}")
                if s == SERIAL_CFG["done_token"]:
                    break

            print("[Serial] The command is complete.")

        except serial.SerialException as e:
            print(f"[Serial] Serial Connection Failed: {e}")
            ports = get_detected_ports()
            if ports:
                print(f"[Serial] Detected ports: {ports}")
            else:
                print("[Serial] No ports detected by pyserial.")
        except Exception as e:
            print(f"[Serial] Unexpected error: {e}")
        finally:
            if ser is not None:
                try:
                    ser.close()
                    print(f"[Serial] Closed {port}")
                except Exception:
                    pass

# Main program. All execution starts here
def main():
    root = tk.Tk()
    try:
        style = ttk.Style()
        if "clam" in style.theme_names():
            style.theme_use("clam")
    except Exception:
        pass

    # Create an instance of the PETScannerGUI class
    PETScannerGUI(root)
    
    # Make the GUI live, continually waiting for input and executing user requests
    root.mainloop()


if __name__ == "__main__":
    main()