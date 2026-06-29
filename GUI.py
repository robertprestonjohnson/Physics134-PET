import threading
import time
import tkinter as tk
from tkinter import ttk
import tkinter.font as tkfont
import sys
import ctypes
from scipy.signal import find_peaks

# Windows API constants
ES_CONTINUOUS       = 0x80000000
ES_SYSTEM_REQUIRED  = 0x00000001
ES_DISPLAY_REQUIRED = 0x00000002 # include this below if you want the display to stay on

def prevent_sleep():
    ctypes.windll.kernel32.SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED)
        
def allow_sleep():
    ctypes.windll.kernel32.SetThreadExecutionState(ES_CONTINUOUS)
    
import serial
try:
    from serial.tools import list_ports
except Exception:
    list_ports = None

from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

import os
import re

import struct
from itertools import zip_longest
import csv
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from matplotlib import colors
import matplotlib.cm as cm
from scipy.optimize import curve_fit
from scipy.signal import find_peaks, peak_widths
import math

# UART parameters for the communication between PC and Red Pitaya. These must match what is set in PET_RP_Slave.cpp
SERIAL_CFG = {
    "baudrate": 115200,
    "timeout": 20,
    "handshake_send": b"R U THERE?",
    "handshake_expect": b"YES I AM\n",
    "done_token": b"DONE\n",
}

# Default values for the parameters that can be set by the GUI. A few no longer appear in the GUI, for safety (e.g. "write").
DEFAULTS = {
    "angleRot": "15",
    "angle0": "0",
    "numAngles": "2",
    "angleStep": "20",

    "nsteps": "2",
    "x0": "30",
    "stepsize": "5",
    "dwelltime": "10",

    "filename": "analysisData.csv",
    "tempFile": "tempData.csv",

    "number": "100",
    "time": "0",
    "trgtype": "external",
    "trigchan": "chA",

    "trglev": "0.2",
    "trghyst": "0",
    "coincwindow": "1",
    "pulsethresh": "10",

    "mean": "511",
    "sigma": "25.0",
    "calibA": "10.0",
    "calibB": "10.0",

    "gamMax": "1500",
    "numBins": "150",
    "histtype": "integral",

    "timingA": "30",
    "timingB": "30",
    
    "pedA": "0.038738",
    "pedB": "0.042397",
    "autoPed": "no",

    "maxscope": "0",

    "dac": "chA",
    "mvolts": "200",
    "write": "no",
    
    "HVdac": "chA",
    "volts": "20",

    "direction": "left",
    "distance": "5",

    "datafile": "histData.csv",
    
    "abortStatus": "NO",
    
    "bckgndFit": "yes",
}

# Define parameters that appear in the list on the left in the GUI and take numerical or string values
NUM_PARAMS = [
    {"key": "angleRot",    "label": "Angle Rot",   "prefix": "-g", "dtype": "float"},
    {"key": "angle0",      "label": "Angle0",      "prefix": "-G", "dtype": "float"},
    {"key": "numAngles",   "label": "Num Angles",  "prefix": "-L", "dtype": "int"},
    {"key": "angleStep",   "label": "Angle Step",  "prefix": "-s", "dtype": "float"},

    {"key": "nsteps",      "label": "Steps",       "prefix": "-N", "dtype": "int"},
    {"key": "x0",          "label": "X0",          "prefix": "-0", "dtype": "float"},
    {"key": "stepsize",    "label": "Step Size",   "prefix": "-X", "dtype": "float"},
    {"key": "dwelltime",   "label": "Dwell Time",  "prefix": "-W", "dtype": "float"},

    {"key": "number",      "label": "Number",      "prefix": "-n", "dtype": "int"},
    {"key": "time",        "label": "Time",        "prefix": "-T", "dtype": "float"},

    {"key": "trglev",      "label": "Trglev",      "prefix": "-e", "dtype": "float"},
    {"key": "trghyst",     "label": "Trghyst",     "prefix": "-H", "dtype": "float"},
    {"key": "coincwindow", "label": "Coinc Window","prefix": "-i", "dtype": "int"},
    {"key": "pulsethresh", "label": "Pulse Thresh","prefix": "-r", "dtype": "float"},

    {"key": "mean",        "label": "Mean",        "prefix": "-M", "dtype": "float"},
    {"key": "sigma",       "label": "Sigma",       "prefix": "-S", "dtype": "float"},
    {"key": "calibA",      "label": "Calib A",     "prefix": "-Y", "dtype": "float"},
    {"key": "calibB",      "label": "Calib B",     "prefix": "-Z", "dtype": "float"},
    {"key": "maxscope",    "label": "maxScope",    "prefix": "-y", "dtype": "int"},

    {"key": "gamMax",      "label": "Max Energy",     "prefix": "-m", "dtype": "float"},
    {"key": "numBins",     "label": "Num Bins",    "prefix": "-U", "dtype": "int"},

    {"key": "timingA",     "label": "Timing A",    "prefix": "-a", "dtype": "int"},
    {"key": "timingB",     "label": "Timing B",    "prefix": "-b", "dtype": "int"},
    
    {"key": "pedA", "label": "PED A", "prefix": "-A", "dtype": "float"},
    {"key": "pedB", "label": "PED B", "prefix": "-B", "dtype": "float"},

    {"key": "mvolts",      "label": "mvolts",      "prefix": "-v", "dtype": "int"},
    {"key": "volts",       "label": "volts",       "prefix": "-v", "dtype": "int"},
    {"key": "distance",    "label": "Distance",    "prefix": "-x", "dtype": "float"},

    {"key": "datafile",    "label": "Datafile",    "prefix": "-f", "dtype": "str"},
]

# This paramter is treated separately in the code from those above, although it isn't clear why. . .
CTRL_FILE = {"key": "filename", "label": "File", "prefix": "-F", "dtype": "str"}

# Define parameters that appear in the list on the left in the GUI and have drop-down menus
DROPDOWNS = [
    {"key": "trgtype",   "label": "Trgtype",   "prefix": "-t", "options": ("external", "internal")},
    {"key": "trigchan",  "label": "TrigChan",  "prefix": "-c", "options": ("chA", "chB")},
    {"key": "write",     "label": "Write",     "prefix": "-w", "options": ("yes", "no")},
    {"key": "direction", "label": "Direction", "prefix": "-d", "options": ("left", "right")},
    {"key": "dac", "label": "DAC", "prefix": "-D", "options": ("chA", "chB")},
    {"key": "HVdac", "label": "HVDAC", "prefix": "-C", "options": ("chA", "chB")},
    {"key": "histtype", "label": "histType",   "prefix": "-Q", "options": ("integral", "peak")},
    {"key": "bckgndFit", "label": "bckgndfit", "prefix": " ", "options": ("yes", "no")},
    {"key": "autoPed", "label": "AutoPed", "prefix": "-p", "options": ("yes", "no")},
]

# Define the command parameters that each action button will send to the Red Pitaya main program PET_RP_Slave.cpp 
BUTTON_PARAM_SETS = {
    "Scan": [
        "filename",
        "angle0", "numAngles", "angleStep",
        "nsteps", "x0", "stepsize", "dwelltime",
        "number", "time",
        "mean", "sigma",
        "calibA", "calibB",
        "autoPed",
        "pedA", "pedB",
        "coincwindow", "pulsethresh",
        "gamMax", "numBins", "histtype",
    ],
    "Acquire Data": [
        "filename",
        "number", "time",
        "trigchan", "trgtype",
        "trglev", "trghyst",
        "mean", "sigma",
        "calibA", "calibB",
        "autoPed",
        "pedA", "pedB",
        "coincwindow", "pulsethresh",
        "gamMax", "numBins", "maxscope",
        "histtype",
    ],
    "Move Stage": [
        "nsteps", "x0", "stepsize", "dwelltime",
        "direction", "distance",
    ],
    "Rotate Stage": [
        "angleRot",
    ],
    "Set Timing": [
        "timingA", "timingB",
    ],
    "Set Threshold": [
        "dac", "mvolts",
    ],
    "Set HV": [
        "HVdac", "volts",
    ],
    "Graph": [],
    "Spectrum": [],
    "Abort": [],
}

# Define for each action button the name of the corresponding command to be interpreted by PET_RP_Slave.cpp
BUTTON_COMMAND_WORD = {
    "Scan": "scan",
    "Acquire Data": "acquireData",
    "Move Stage": "moveStage",
    "Rotate Stage": "rotateStage",
    "Set Timing": "setTiming",
    "Set Threshold": "setTHR",
    "Set HV": "setHV",
    "Graph": "graph",
    "Spectrum": "Spectrum",
    "Abort": "abort",
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

        try:
            style = ttk.Style()
            if "clam" in style.theme_names():
                style.theme_use("clam")
        except Exception:
            style = ttk.Style()

        primary_bg = "#223A72"
        primary_text = "#ECEFF4"
        muted_text = "#5F9EA0"
        accent_cyan = "#88C0D0"
        button_active_bg = "#81A1C1"
        dark_trough_1 = "#3B4252"
        dark_trough_2 = "#4C566A"

        self.sidebar_heading_font = tkfont.Font(family="Segoe UI", size=12, weight="bold")
        self.root.configure(bg=primary_bg)
        self.root.option_add("*Font", ("Segoe UI", 10))
        try:
            default_font = tkfont.nametofont("TkDefaultFont")
            default_font.configure(family="Segoe UI", size=10)
            text_font = tkfont.nametofont("TkTextFont")
            text_font.configure(family="Segoe UI", size=10)
            fixed_font = tkfont.nametofont("TkFixedFont")
            fixed_font.configure(family="Segoe UI", size=10)
        except Exception:
            pass

        style.configure("Main.TFrame", background=primary_bg)
        style.configure("TFrame", background=primary_bg)
        style.configure("TLabel", background=primary_bg, foreground=primary_text, font=("Segoe UI", 10))
        style.configure("Bold.TLabel", background=primary_bg, foreground=primary_text, font=("Segoe UI", 14, "bold"))
        style.configure("TButton", font=("Segoe UI", 10, "bold"))
        style.map("TButton", background=[("active", button_active_bg)])
        style.configure("TEntry", fieldbackground=dark_trough_2, foreground=primary_text)
        style.configure("TCombobox", fieldbackground=dark_trough_2, foreground=primary_text)
        style.map("TCombobox", fieldbackground=[("readonly", dark_trough_2)])
        style.configure("TSeparator", background=dark_trough_2)
        

        style.configure("TScale",
                        troughcolor=dark_trough_1,
                        background=accent_cyan,
                        sliderthickness=40,
                        sliderlength=50)

        style.configure("Vertical.TScrollbar", background=primary_bg, troughcolor=dark_trough_1, arrowcolor=primary_text)

        self.root.title("PET Scanner")
        self.root.minsize(900, 550)

        title_frame = ttk.Frame(root, padding=(10, 8), style="Main.TFrame")
        title_frame.grid(row=0, column=0, sticky="ew")
        title_frame.columnconfigure(0, weight=1)
        ttk.Label(title_frame, text="PET Scanner", font=("Segoe UI", 18, "bold"), style="TLabel").grid(row=0, column=0, sticky="w")

        main = ttk.Frame(root, style="Main.TFrame")
        main.grid(row=1, column=0, sticky="nsew")
        root.rowconfigure(1, weight=1)
        root.columnconfigure(0, weight=1)

        main.columnconfigure(0, weight=0)
        main.columnconfigure(1, weight=1)
        main.rowconfigure(0, weight=1)

        left_container = ttk.Frame(main, style="Main.TFrame")
        left_container.grid(row=0, column=0, sticky="nsw")
        left_container.grid_propagate(False)
        self.left_min_width = 420
        left_container.configure(width=self.left_min_width)

        right = ttk.Frame(main, style="Main.TFrame")
        right.grid(row=0, column=1, sticky="nsew")
        right.rowconfigure(0, weight=1)
        right.rowconfigure(1, weight=0)
        right.columnconfigure(0, weight=1)

        self.root.bind("<Configure>", lambda e: self._on_resize(left_container))

        self.left_canvas = tk.Canvas(left_container, highlightthickness=0, bg=primary_bg)
        canvas = self.left_canvas
        vsb = ttk.Scrollbar(left_container, orient="vertical", command=canvas.yview, style="Vertical.TScrollbar")
        canvas.configure(yscrollcommand=vsb.set)
        canvas.grid(row=0, column=0, sticky="nsew")
        vsb.grid(row=0, column=1, sticky="ns")
        left_container.rowconfigure(0, weight=1)
        left_container.columnconfigure(0, weight=1)

        self.left_inner = ttk.Frame(canvas, padding=(10, 10), style="Main.TFrame")
        inner_id = canvas.create_window((0, 0), window=self.left_inner, anchor="nw")

        def _on_inner_config(_evt):
            canvas.configure(scrollregion=canvas.bbox("all"))
            canvas.itemconfig(inner_id, width=canvas.winfo_width())

        self.left_inner.bind("<Configure>", _on_inner_config)

        # Define tkinter variables
        self.vars = {}
        self.widgets = {}

        # Try to get smart about setting the default COM port. If this doesn't work, then the user will have to set the correct port
        detected = get_detected_ports()
        fallback = [f"COM{i}" for i in range(1, 8)]
        self.com_options = detected + [p for p in fallback if p not in detected]

        # default COM selection: try COM5 if it exists, else first detected, else COM5
        default_com = "COM5" if "COM5" in self.com_options else (self.com_options[0] if self.com_options else "COM5")
        self.vars["com_port"] = tk.StringVar(value=default_com)

        self.vars["filename"] = tk.StringVar(value=DEFAULTS["filename"])
        self.vars["tempFile"] = tk.StringVar(value=DEFAULTS["tempFile"])
        self.vars["abortStatus"] = tk.StringVar(value=DEFAULTS["abortStatus"])
        self.vars["bckgndFit"] = tk.StringVar(value=DEFAULTS["bckgndFit"])

        # numeric and string left variables defined in the NUM_PARAMS list
        for p in NUM_PARAMS:
            self.vars[p["key"]] = tk.StringVar(value=DEFAULTS[p["key"]])

        # dropdown variables
        for d in DROPDOWNS:
            self.vars[d["key"]] = tk.StringVar(value=DEFAULTS[d["key"]])

        self.vars["last_angle"] = tk.StringVar(value="—")
        self.vars["plot_status"] = tk.StringVar(value="")
        self.vars["status_msg"] = tk.StringVar(value="")

        self.vcmd_int = (root.register(self._validate_int), "%P")
        self.vcmd_float = (root.register(self._validate_float), "%P")
        self.vcmd_filename = (root.register(self._validate_filename), "%P")

        self._build_left_panel()
        self._build_graph(right, primary_bg)
        self._build_control_strip(right, muted_text)

        self.root.after_idle(lambda: self.left_canvas.yview_moveto(0.0))

        self.vars["angle0"].trace_add("write", self._clamp_angle0)
        self.vars["trgtype"].trace_add("write", self._on_trgtype_change)
        self._on_trgtype_change()
        self.vars["histtype"].trace_add("write", self._on_histtype_change)

    def _on_resize(self, left_container: ttk.Frame):
        w = self.root.winfo_width()
        target = int(w * 0.32)
        left_w = max(self.left_min_width, target)
        left_container.configure(width=left_w)

    def _build_left_panel(self):
        top = ttk.Frame(self.left_inner, style="Main.TFrame")
        top.grid(row=0, column=0, columnspan=4, sticky="ew", pady=(0, 10))
        ttk.Label(top, text="COM Port", style="TLabel").grid(row=0, column=0, sticky="w", padx=(0, 8))
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

        # Function to create a box in which to enter data in the left panel
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

        # Function to create a dropdown menu in the left panel
        def make_dropdown(parent, key: str):
            spec = dd_by_key[key]
            return ttk.Combobox(
                parent,
                textvariable=self.vars[key],
                values=list(spec["options"]),
                state="readonly",
                width=12,
            )
            
        # Function to add a text heading above a set of GUI variables in the left panel
        def add_heading(r: int, text: str) -> int:
            ttk.Label(self.left_inner, text=text, font=self.sidebar_heading_font, style="TLabel").grid(
                row=r, column=0, columnspan=4, sticky="w", pady=(8, 2)
            )
            ttk.Separator(self.left_inner, orient="horizontal").grid(
                row=r + 1, column=0, columnspan=4, sticky="ew", pady=(0, 6)
            )
            return r + 2

        # Function to create a row of drop-down menues and/or data boxes in the left panel
        def add_row(r: int, left_label, left_key, right_label=None, right_key=None, right_is_filename=False, store_left=False, store_right=False):
            ttk.Label(self.left_inner, text=left_label, style="TLabel").grid(row=r, column=0, sticky="w", padx=(0, 12), pady=3)
            if left_key in dd_by_key:
                w1 = make_dropdown(self.left_inner, left_key)
            elif left_key == "filename":
                w1 = ttk.Entry(self.left_inner, textvariable=self.vars["filename"], width=20,
                               validate="key", validatecommand=self.vcmd_filename)
            else:
                w1 = make_entry(self.left_inner, left_key)
            w1.grid(row=r, column=1, sticky="ew", pady=3)
            if store_left:
                self.widgets[left_key] = w1

            if right_label is not None and right_key is not None:
                ttk.Label(self.left_inner, text=right_label, style="TLabel").grid(row=r, column=2, sticky="w", padx=(18, 12), pady=3)
                if right_is_filename or right_key == "filename":
                    w2 = ttk.Entry(self.left_inner, textvariable=self.vars["filename"], width=20,
                                   validate="key", validatecommand=self.vcmd_filename)
                elif right_key in dd_by_key:
                    w2 = make_dropdown(self.left_inner, right_key)
                else:
                    w2 = make_entry(self.left_inner, right_key)
                w2.grid(row=r, column=3, sticky="ew", pady=3)
                if store_right:
                    self.widgets[right_key] = w2
            else:
                ttk.Label(self.left_inner, text="", style="TLabel").grid(row=r, column=2, sticky="w", pady=3)
                ttk.Label(self.left_inner, text="", style="TLabel").grid(row=r, column=3, sticky="w", pady=3)

        r = 1

        r = add_heading(r, "Set Threshold")
        add_row(r, "Channel", "dac", "mVolts", "mvolts"); r += 1
        
        r = add_heading(r, "Set High Voltage")
        add_row(r, "Channel", "HVdac", "Volts", "volts"); r += 1

        r = add_heading(r, "Set Timing")
        add_row(r, "Timing A", "timingA", "Timing B", "timingB"); r += 1

        r = add_heading(r, "Move or Rotate Stage")
        add_row(r, "Direction", "direction", "Distance", "distance"); r += 1
        add_row(r, "Angle Rot", "angleRot"); r += 1

        r = add_heading(r, "Scan")
        add_row(r, "Angle 0", "angle0", "# Angles", "numAngles"); r += 1
        add_row(r, "Angle Step", "angleStep", "# Steps", "nsteps"); r += 1
        add_row(r, "X0", "x0", "Step Size", "stepsize"); r += 1
        add_row(r, "Dwell Time", "dwelltime", "File", "filename", right_is_filename=True); r += 1

        r = add_heading(r, "Acquire Data")
        add_row(r, "Number", "number", "Time", "time"); r += 1
        add_row(r, "Trg Chan", "trigchan", "Trg Type", "trgtype", store_left=True); r += 1
        add_row(r, "Trg Level", "trglev", "Trg Hyst", "trghyst", store_left=True, store_right=True); r += 1
        add_row(r, "Num Traces", "maxscope", "Auto Ped", "autoPed"); r += 1

        r = add_heading(r, "Scan & Acquire Data")
        add_row(r, "Coinc Win", "coincwindow", "Pulse Thresh", "pulsethresh"); r += 1
        add_row(r, "Mean", "mean", "Sigma", "sigma"); r += 1
        add_row(r, "Calib A", "calibA", "Calib B", "calibB"); r += 1
        add_row(r, "Ped A", "pedA", "Ped B", "pedB"); r += 1
        
        r = add_heading(r, "Spectral Plot")
        add_row(r, "Hist Type", "histtype", "Bckgnd Fit", "bckgndFit"); r += 1
        add_row(r, "Hist Max", "gamMax", "Num Bins", "numBins"); r += 1

        for c in range(4):
            self.left_inner.columnconfigure(c, weight=1)

    # Stuff to do if somebody changes variables affecting the histogram type
    def _on_histtype_change(self, *_):
        t = self.vars["histtype"].get()
        print("t = ", t)
        if t == "peak":
            self.vars["gamMax"].set("1.2")
            self.vars["numBins"].set("120")
        else:
            self.vars["gamMax"].set("1500")
            self.vars["numBins"].set("150")            

    # Stuff to do if somebody changes the trigger type
    def _on_trgtype_change(self, *_):
        t = self.vars["trgtype"].get()
        trigchan_w = self.widgets.get("trigchan")
        trglev_w = self.widgets.get("trglev")
        trghyst_w = self.widgets.get("trghyst")

        if t == "internal":
            if trigchan_w is not None:
                trigchan_w.configure(state="readonly")
            if trglev_w is not None:
                trglev_w.configure(state="normal")
            if trghyst_w is not None:
                trghyst_w.configure(state="normal")
        else:
            if trigchan_w is not None:
                trigchan_w.configure(state="disabled")
            if trglev_w is not None:
                trglev_w.configure(state="disabled")
            if trghyst_w is not None:
                trghyst_w.configure(state="disabled")

    # Function to build the panel where the graph of counts versus position is displayed.
    def _build_graph(self, right: ttk.Frame, primary_bg: str):
        graph_frame = ttk.Frame(right, padding=(10, 10), style="Main.TFrame")
        graph_frame.grid(row=0, column=0, sticky="nsew")
        graph_frame.rowconfigure(0, weight=1)
        graph_frame.columnconfigure(0, weight=1)

        fig = Figure(figsize=(6, 4))
        fig.patch.set_facecolor(primary_bg)
        self.ax = fig.add_subplot(111)
        try:
            self.ax.set_facecolor(primary_bg)
            for spine in self.ax.spines.values():
                spine.set_color("#ECEFF4")
            self.ax.tick_params(colors="#ECEFF4")
            self.ax.xaxis.label.set_color("#ECEFF4")
            self.ax.yaxis.label.set_color("#ECEFF4")
            self.ax.title.set_color("#ECEFF4")
        except Exception:
            pass

        self.canvas = FigureCanvasTkAgg(fig, master=graph_frame)
        self.canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")

    # Function to build the row of command "action" buttons below the graph
    def _build_control_strip(self, right: ttk.Frame, muted_text: str):
        strip = ttk.Frame(right, padding=(10, 8), style="Main.TFrame")
        strip.grid(row=1, column=0, sticky="ew")

        buttons = ["Set Threshold", "Set HV", "Set Timing", "Move Stage", "Rotate Stage", "Acquire Data", "Scan", "Graph", "Spectrum", "Abort"]
        col = 0
        for name in buttons:
            ttk.Button(strip, text=name, command=lambda n=name: self.on_button(n)).grid(row=0, column=col, padx=4)
            col += 1

        ttk.Label(strip, text="Plotted angle:", style="TLabel").grid(row=1, column=0, sticky="w", pady=(6, 0))
        ttk.Label(strip, textvariable=self.vars["last_angle"], style="TLabel").grid(row=1, column=1, sticky="w", pady=(6, 0))
        ttk.Label(strip, textvariable=self.vars["plot_status"], style="TLabel", foreground=muted_text).grid(row=2, column=0, columnspan=col, sticky="w", pady=(6, 0))
        ttk.Label(strip, textvariable=self.vars["status_msg"], style="TLabel").grid(row=2, column=0, columnspan=col, sticky="w", pady=(6, 0))

    # functions to validate data entered by the user
    def _validate_int(self, proposed: str) -> bool:
        return is_valid_int_partial(proposed)

    def _validate_float(self, proposed: str) -> bool:
        return is_valid_float_partial(proposed)

    def _validate_filename(self, proposed: str) -> bool:
        return is_valid_filename_partial(proposed)

    def _clamp_angle0(self, *_):
        s = self.vars["angle0"].get()
        if s in ("", "-", ".", "-."):
            return
        try:
            v = float(s)
        except ValueError:
            return
        if v > 180.0:
            self.vars["angle0"].set("180")
        elif v < 0.0:
            self.vars["angle0"].set("0")

    # Function to build a command to be sent to PET_RP_Slave on the Red Pitaya
    def build_cmd_list(self, button_name: str):
        cmd_word = BUTTON_COMMAND_WORD.get(button_name, button_name)
        keys_needed = BUTTON_PARAM_SETS.get(button_name, [])

        tokens = ["START", cmd_word]

        if "filename" in keys_needed:
            tokens += [CTRL_FILE["prefix"], self._get_token("filename", "str")]

        for p in NUM_PARAMS:
            if p["key"] in keys_needed:
                if p["key"] == "datafile":
                    continue
                tokens += [p["prefix"], self._get_token(p["key"], p["dtype"])]

        for d in DROPDOWNS:
            if d["key"] in keys_needed:
                if d["key"] == "write":
                    continue
                if d["key"] == "bckgndFit":
                    continue
                tokens += [d["prefix"], self.vars[d["key"]].get()]

        tokens.append("END")

        data_to_send = " ".join(tokens)
        print(f"[{button_name}] data_to_send: {data_to_send}")
        return tokens

    def _get_token(self, key: str, dtype: str) -> str:
        if key == "filename":
            raw = self.vars["filename"].get()
            return raw if raw != "" else "0"

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

    # Define a plot status message just below the plot
    def _set_plot_status(self, msg: str):
        self.root.after(0, lambda: self.vars["plot_status"].set(msg))

    # Display status messages coming from the Red Pitaya
    def _set_status_msg(self, msg: str):
        self.root.after(0, lambda: self.vars["status_msg"].set(msg))

    def _try_float(self, v):
        try:
            return float(v)
        except Exception:
            return None
            
    # Parse the scan results returned from the Red Pitaya, in order to build the plot
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

    # Build the main plot displayed by the GUI: counts versus scan position
    def plot_scan_histogram_from_tempfile(self):
        path = self.vars["tempFile"].get().strip()
        if not path:
            self._set_plot_status("Plot error: File box is empty.")
            return

        if not os.path.exists(path):
            self._set_plot_status(f"Plot error: temp file not found locally: {path}")
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

        try:
            self.ax.set_facecolor("#223A72")
            for spine in self.ax.spines.values():
                spine.set_color("#ECEFF4")
            self.ax.tick_params(colors="#ECEFF4")
            self.ax.xaxis.label.set_color("#ECEFF4")
            self.ax.yaxis.label.set_color("#ECEFF4")
            self.ax.title.set_color("#ECEFF4")
        except Exception:
            pass

        width = 1.0
        if len(pos) >= 2:
            diffs = [abs(pos[i + 1] - pos[i]) for i in range(len(pos) - 1)]
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

    # Define what to do when an action button is pushed
    def on_button(self, button_name: str):
        if button_name == "Graph":
            self.root.after(0, self.plot_scan_histogram_from_tempfile)
            return
            
        if button_name == "Spectrum":
            self.root.after(0, self.show_analysis_window)
            return
            
        if button_name == "Abort":
            self.vars["abortStatus"].set("YES")
            return

        cmd_list = self.build_cmd_list(button_name)
        port = self.vars["com_port"].get().strip()

        t = threading.Thread(target=self._serial_worker, args=(port, cmd_list), daemon=True)
        t.start()

    # This routine starts each time communication with the PET_RP_Slave code on the Red Pitaya is needed
    def _serial_worker(self, port: str, cmd_list):
        ser = None
        window = None
        windowPulse = None
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

            # Send a simple handshake message to the Red Pitaya
            print(f"[Serial] Sending {SERIAL_CFG['handshake_send']!r}")
            ser.write(SERIAL_CFG["handshake_send"])
            reply = ser.readline()
            print(f"[Serial] Reply is {reply!r}")

            # Check that the expected reply is returned by the Red Pitaya. If not, then PET_RP_Slave.cpp probably isn't executing.
            if reply != SERIAL_CFG["handshake_expect"]:
                print("Handshake Protocol Failed; Closing Com Port")
                return

            self.vars["abortStatus"].set("NO")
            scanCMD = False
            for token in cmd_list:
                b = token.encode("ascii")
                if "scan" in token: scanCMD = True
                #print(f"[Serial] Sending {b!r}")
                ser.write(b)
                s = ser.readline()
                #print(f"[Serial] Reply is {s!r}")

            firstData = True
            firstAna = True
            scanDataPath = self.vars["tempFile"].get().strip()
            anaDataPath = self.vars["filename"].get().strip()
            prevent_sleep()   # don't allow the computer to sleep while a command is executing
            while True:
                s = ser.readline()
                print(f"[Serial] Message is {s!r}")
                if s == SERIAL_CFG["done_token"]:
                    break
                msgString = s.decode('utf-8');
                if "STATUS:" in msgString: 
                    self._set_status_msg(msgString)
                    if scanCMD and "ready to rotate to next angle" in msgString:
                        self.plot_scan_histogram_from_tempfile()
                        firstData = True
                    time.sleep(2)     # just to guarantee a bit of time to read the message
                if "SCANDATA:" in msgString:
                    fileData = msgString.replace("SCANDATA: ", "")
                    if firstData:
                        try:
                            with open(scanDataPath, 'w') as scanFile:
                                scanFile.write(fileData)
                        except Exception as e:
                            print(f"An error {e} occurred while creating new file to write scan data")
                        firstData = False
                    else:
                        try:
                            with open(scanDataPath, 'a') as scanFile:
                                scanFile.write(fileData)
                        except Exception as e:
                            print(f"An error {e} occurred while opening file to append scan data") 
                if "HISTOGRAM:" in msgString:
                    if window is not None and window.winfo_exists():
                        window.destroy()
                    got_histograms = True
                    try:
                        Nbins = int(self.vars["numBins"].get().strip())
                        s = ser.read(4*Nbins)
                        print("Received ", sys.getsizeof(s), " bytes")
                        bsn = ser.read(1)
                        print(bsn)            
                        print("Binary histogram data, channel A: ", s)                        
                        list_of_intsA = struct.unpack('<' + str(Nbins) + 'I', s[0:4*Nbins])   
                        print("Integer histogram data of length ", len(list_of_intsA), "  Data= ", list_of_intsA)
                        s = ser.read(4*Nbins)       
                        print("Received ", sys.getsizeof(s), " bytes")
                        bsn = ser.read(1)
                        print(bsn)     
                        print("Binary histogram data, channel B: ", s)     
                        list_of_intsB = struct.unpack('<' + str(Nbins) + 'I', s[0:4*Nbins])   
                        print("Integer histogram data of length ", len(list_of_intsB), "  Data= ", list_of_intsB)                   
                        with open("HistData.csv", "w", newline="") as temp:
                            write = csv.writer(temp)
                            for a,b in zip_longest(list_of_intsA, list_of_intsB, fillvalue=""):
                                write.writerow([a, b])
                    except Exception as e:
                        print(f"Error in getting the spectral histograms: {e}")
                        got_histograms = False
                    if got_histograms: 
                        window = self.plot_histograms()
                        print("Spectral histogram window created")
                if "PULSES:" in msgString:
                    Npulses = int(self.vars["maxscope"].get().strip())
                    if Npulses == 1 and windowPulse is not None and windowPulse.winfo_exists():
                        windowPulse.destroy()
                    got_pulses = True
                    try:
                        Nbytes = int(msgString.replace("PULSES:", "").strip())
                        Nfloats = math.floor(Nbytes/4)
                        s = ser.read(Nbytes)
                        print("Received ", sys.getsizeof(s), " bytes of pulse data for detector A")
                        print("Binary pulse data, channel A: ", s)                        
                        list_of_floatsA = struct.unpack('<' + str(Nfloats) + 'f', s[0:4*Nfloats])   
                        print("Floating-point data of length ", len(list_of_floatsA), "  Data= ", list_of_floatsA)
                        s = ser.read(Nbytes)       
                        print("Received ", sys.getsizeof(s), " bytes of pulse data for detector B")
                        print("Binary histogram data, channel B: ", s)     
                        list_of_floatsB = struct.unpack('<' + str(Nfloats) + 'f', s[0:4*Nfloats])   
                        print("Floating-point data of length ", len(list_of_floatsB), "  Data= ", list_of_floatsB)                   
                        with open("ScopeData.csv", "w", newline="") as temp:
                            write = csv.writer(temp)
                            for a,b in zip_longest(list_of_floatsA, list_of_floatsB, fillvalue=""):
                                write.writerow([a, b])
                    except Exception as e:
                        print(f"Error in getting the oscilloscope data: {e}")
                        got_pulses = False
                    if got_pulses: 
                        print("Creating oscilloscope plot window") 
                        windowPulse = self.plot_oscilloscope(Nfloats)                   
                if "ANALYSIS:" in msgString:
                    fileData = msgString.replace("ANALYSIS: ", "")
                    if firstAna:
                        try:
                            with open(anaDataPath, 'w') as scanFile:
                                scanFile.write(fileData)
                        except Exception as e:
                            print(f"An error {e} occurred while creating new file to write Spectrum data")
                        firstAna = False
                    else:
                        try:
                            with open(anaDataPath, 'a') as scanFile:
                                scanFile.write(fileData)
                        except Exception as e:
                            print(f"An error {e} occurred while opening file to append Spectrum data")  
                if "ABORT?" in msgString:
                    ser.write(self.vars["abortStatus"].get().strip().encode("ascii"))
                    if self.vars["abortStatus"].get().strip() == "YES":
                        self.vars["abortStatus"].set("NO")
                        print("Aborting current command by user request.")
                        self._set_status_msg("Command was aborted by user request.")
                            
            allow_sleep()
            print("[Serial] The command is complete.")
            if scanCMD: self._set_status_msg("The scan is complete and analysis data were written to " + anaDataPath)

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

    # Create a new window to display the spectral histograms
    def show_analysis_window(self):
        def sndcmd(cmdList):
            for cmd in cmdList:
                data_to_send = cmd;
                print("Sending ", data_to_send.encode('ascii'))
                ser.write(data_to_send.encode('ascii'))
                s = ser.readline()
                print("Reply is ", s)  # This should print out "Ack Token" to acknowledge reception of the token
            while True:     # Print out messages that come back during the command execution
                s = ser.readline()
                print("Message is ", s)     # Empty messages will get printed if the readline() times out
                if s == b'DONE\n': break     # A "DONE" mesage should come back when the execution terminates
              
        print("Sending test data")  
        try:
            ser = serial.Serial(port='COM5',baudrate=115200,stopbits=serial.STOPBITS_ONE,parity=serial.PARITY_NONE,bytesize=serial.EIGHTBITS,timeout=20)
            time.sleep(1)
            print(ser.name)

            # Send a handshake message to see if the RP is alive. . .
            data_to_send = "R U THERE?"
            print("Sending ", data_to_send.encode('ascii'))
            ser.write(data_to_send.encode('ascii'))
            s = ser.readline()       # Every text message coming back from the RP should have \n at the end, so that readline will work
            print("Reply is ", s)    # This should print out "Yes I am"
            
            Nbins = int(self.vars["numBins"].get().strip())
            
            data_to_send = "Send Histogram A"
            ser.write(data_to_send.encode('ascii'))
            time.sleep(0.2)
            s = ser.read(4*Nbins)
            print("Received ", sys.getsizeof(s), " bytes")
            bsn = ser.read(1)
            print(bsn)            
            print("Binary histogram data, channel A: ", s)     
            
            #unpack in little-endian format Nbins unsigned ints from 4 times as many bytes
            list_of_intsA = struct.unpack('<' + str(Nbins) + 'I', s[0:4*Nbins])   
            print("Integer histogram data of length ", len(list_of_intsA), "  Data= ", list_of_intsA)
            
            data_to_send = "Send Histogram B"
            ser.write(data_to_send.encode('ascii'))
            time.sleep(0.2)
            s = ser.read(4*Nbins)       
            print("Received ", sys.getsizeof(s), " bytes")
            bsn = ser.read(1)
            print(bsn)                 
            print("Binary histogram data, channel B: ", s)    # This should print out binary data 
            list_of_intsB = struct.unpack('<' + str(Nbins) + 'I', s[0:4*Nbins])   
            print("Integer histogram data of length ", len(list_of_intsB), "  Data= ", list_of_intsB)
            
            with open("HistData.csv", "w", newline="") as temp:
                write = csv.writer(temp)
                for a,b in zip_longest(list_of_intsA, list_of_intsB, fillvalue=""):
                    write.writerow([a, b])
        
        except serial.SerialException as e:
            print(f"Serial Connection Failed: {e}")
            ser = None
            return
        except Exception as e:
            print(f"Error in getting the spectral histograms: {e}")
            return
        self.plot_histograms()
            
    # Function to plot and fit the spectral histograms
    def plot_histograms(self):   
        def gaussian(x, amplitude, mean, sigma):
            return amplitude * np.exp(-((x - mean) ** 2) / (2 * sigma ** 2))
            
        def poly(x, c0, c1, c2, c3):
            return c0 + (c1 + (c2 + x*c3)*x)*x
            
        def gausPlusPoly(x, amp, mu, sig, c0, c1, c2, c3):
            return poly(x, c0, c1, c2, c3) + gaussian(x, amp, mu, sig)
            
        initial_guess_with_bkg = [25.0, float(self.vars["mean"].get().strip()), float(self.vars["sigma"].get().strip()), 0., 0., 0., 0. ]
        initial_guess = [25.0, float(self.vars["mean"].get().strip()), float(self.vars["sigma"].get().strip())]
        fitBkg = False
        print("bckgndFit var = ",self.vars["bckgndFit"].get())
        if self.vars["bckgndFit"].get() == "yes": fitBkg = True       

        Nbins = int(self.vars["numBins"].get().strip())
        print("Number of histogram bins = ", Nbins)
        max_x = float(self.vars["gamMax"].get().strip())
        print("Maximum histogram energy = ", max_x)
        print("Plot the spectral histogram now")
        # Use your GUI variable if available; fall back to histData.csv
        file_path = self.vars["datafile"].get().strip() if "datafile" in self.vars else "histData.csv"
        if not file_path:
            file_path = "histData.csv"

        try:
            print("Read the histogram data from " + file_path)
            data = pd.read_csv(file_path, header=None, names=["1", "2"])
        except Exception as e:
            self._set_plot_status(f"Spectral histogram error: {e}")
            return

        print("Data to be plotted:")
        print(data)
        
        win = tk.Toplevel(self.root)
        win.title("Histogram of Sensor Data")
        win.geometry("1000x450")
        win.configure(bg="#223A72")
        win.rowconfigure(0, weight=1)
        win.columnconfigure(0, weight=1)

        fig = Figure(figsize=(10, 4), dpi=100)
        fig.patch.set_facecolor("#223A72")
        fig.subplots_adjust(left=0.08, right=0.97, bottom=0.14, top=0.90, wspace=0.25)

        ax1 = fig.add_subplot(1, 2, 1)
        ax2 = fig.add_subplot(1, 2, 2)
        axes = [ax1, ax2]

        for ax in axes:
            ax.set_facecolor("#223A72")
            for spine in ax.spines.values():
                spine.set_color("#ECEFF4")
            ax.tick_params(colors="#ECEFF4")
            ax.xaxis.label.set_color("#ECEFF4")
            ax.yaxis.label.set_color("#ECEFF4")
            ax.title.set_color("#ECEFF4")

        bin_edges = np.linspace(0.0, max_x, Nbins + 1)
        midpoints = 0.5 * (bin_edges[:-1] + bin_edges[1:])

        def colored_hist(ax, values, title):
            ax.plot(midpoints, values, drawstyle='steps')

            ax.set_title(title,fontsize=24)
            if self.vars["histtype"].get() == "integral":
                ax.set_xlabel("⚡Energy (keV)⚡",fontsize=18)
            else:
                ax.set_xlabel("⚡Volts⚡",fontsize=18)
            ax.set_ylabel("Count",fontsize=18)
            ax.grid(True, linestyle="--", alpha=0.4)
            
            maxC = 1
            for C in values:
                if C>maxC: maxC = C

            total_entries = np.sum(values)

            try:
                if fitBkg:
                    # Try to fix a gaussian with the provided mean and sigma plus a 3rd-order polynomial going from the noise peak to just above the gaussian 
                    peaks, _ = find_peaks(values)
                    first_max_idx = peaks[0]
                    print(f"First local max index: {first_max_idx}, value_y: {values[first_max_idx]}, value_x: {midpoints[first_max_idx]}")
                    gausRange = [midpoints[first_max_idx], initial_guess_with_bkg[1] + 6.0*initial_guess_with_bkg[2]]
                else: 
                    gausRange = [initial_guess[1] - 6.0*initial_guess[2], initial_guess[1] + 6.0*initial_guess[2]]                    
                mask = (midpoints >= gausRange[0]) & (midpoints <= gausRange[1])
                X = midpoints[mask]
                Y = values[mask]

                if len(X) >= 3 and np.any(Y):
                    if fitBkg:
                        popt, _ = curve_fit(gausPlusPoly, X, Y, p0=initial_guess_with_bkg, maxfev=5000)
                        amplitude_fit, mean_fit, sigma_fit, c0_fit, c1_fit, c2_fit, c3_fit = popt
                        ax.plot(X, gausPlusPoly(X, *popt), color="red", linewidth=1)
                    else:
                        popt, _ = curve_fit(gaussian, X, Y, p0=initial_guess, maxfev=5000)
                        amplitude_fit, mean_fit, sigma_fit = popt
                        ax.plot(X, gaussian(X, *popt), color="red", linewidth=1)
                        
                    ax.annotate(
                        f"{total_entries} Entries\nmu: {mean_fit:.3f}\nsigma: {abs(sigma_fit):.3f}\namp: {amplitude_fit:.1f}",
                        xy=(0.99, 0.99),
                        xycoords="axes fraction",
                        ha="right",
                        va="top",
                        fontsize=10,
                        color="white",
                        bbox=dict(boxstyle="round,pad=0.3", fc="black", ec="none", alpha=0.45),
                    )
            except Exception as e:
                print(f"Gaussian fit failed for {title}: {e}")

            return maxC

        ymx1 = colored_hist(ax1, data["1"], "Detector A")
        ymx2 = colored_hist(ax2, data["2"], "Detector B")

        max_y = max(ymx1, ymx2) + 5
        ax1.set_xlim(0.0, max_x)
        ax2.set_xlim(0.0, max_x)
        ax1.set_ylim(0.0, ymx1)
        ax2.set_ylim(0.0, ymx2)

        canvas = FigureCanvasTkAgg(fig, master=win)
        canvas.draw()
        canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")
        return win

    # Function to display oscilloscope-like traces of the Red Pitaya digitizations
    def plot_oscilloscope(self, nPoints):   
        print("Plot the oscilloscope traces now for " + str(nPoints) + " points")
        
        file_path = "scopeData.csv"
        try:
            print("Read the oscilloscope data from " + file_path)
            data = pd.read_csv(file_path, header=None, names=["1", "2"])
        except Exception as e:
            self._set_plot_status(f"Oscilloscope data error: {e}")
            return

        print("Data to be plotted:", len(data["1"]))
        print(data)
        
        win = tk.Toplevel(self.root)
        win.title("Oscilloscope Traces")
        win.geometry("1000x450")
        win.configure(bg="#223A72")
        win.rowconfigure(0, weight=1)
        win.columnconfigure(0, weight=1)

        fig = Figure(figsize=(10, 4), dpi=100)
        fig.patch.set_facecolor("#223A72")
        fig.subplots_adjust(left=0.08, right=0.97, bottom=0.14, top=0.90, wspace=0.25)

        ax = fig.add_subplot(1, 1, 1)
        ax.set_facecolor("#223A72")
        for spine in ax.spines.values():
            spine.set_color("#ECEFF4")
        ax.tick_params(colors="#ECEFF4")
        ax.xaxis.label.set_color("#ECEFF4")
        ax.yaxis.label.set_color("#ECEFF4")
        ax.title.set_color("#ECEFF4")
        ax.set_title("Oscilloscope Traces",fontsize=24)
        ax.set_xlabel(" Time (ns) ",fontsize=18)
        ax.set_ylabel("Voltage",fontsize=18)
        ax.grid(True, linestyle="--", alpha=0.4)
        
        # The 8.01 ns period corresponds to "decimation" 1 in the Red Pitaya
        RP_period = 8.01
        midpoints = np.linspace(1, nPoints * RP_period, nPoints)
        ax.plot(midpoints, data["1"], color = "red", linewidth = 2, drawstyle='steps', label='Channel A')   
        ax.plot(midpoints, data["2"], color = "green", linewidth = 2, drawstyle='steps', label='Channel B') 
        ax.legend(loc='best')
        
        def mxData(values):  
            maxC = -999.
            for C in values:
                if C>maxC: maxC = C
            return maxC
        def mnData(values):  
            minC = 999.
            for C in values:
                if C<minC: minC = C
            return minC

        ymn1 = mnData(data["1"])
        ymx1 = mxData(data["1"])
        ymn2 = mnData(data["2"])
        ymx2 = mxData(data["2"])

        max_y = max(ymx1, ymx2) + 5
        ax.set_xlim(0., nPoints * RP_period)
        ax.set_ylim(1.10 * min(0.,ymn1,ymn2), 1.10 * max(ymx1,ymx2))

        canvas = FigureCanvasTkAgg(fig, master=win)
        canvas.draw()
        canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")
        return win

# Main program. All execution starts here
def main():
    print("PET scanner GUI version 1.1 starting.")
    root = tk.Tk()
    PETScannerGUI(root)  # Create an instance of the PETScannerGUI class
    root.mainloop()      # Make the GUI live, continually waiting for input and executing user requests


if __name__ == "__main__":
    main()