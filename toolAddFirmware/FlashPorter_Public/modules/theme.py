"""
FlashPorter Theme Module
Light theme — violet primary with pink + mint accents, soft pastel palette.
"""

import tkinter as tk
from tkinter import ttk

# ============================================================
# COLOR PALETTE — light theme (violet / pink / mint)
# ============================================================
class Colors:
    # Primary — violet
    PRIMARY = "#7C5CFC"         # Violet
    PRIMARY_DARK = "#6442E0"    # Deep violet
    PRIMARY_LIGHT = "#9B82FF"   # Light violet

    # Accent — rose / pink
    ACCENT = "#EC4899"          # Rose
    ACCENT_LIGHT = "#F472B6"    # Light pink

    # Success / Error / Warning
    # Picked so each reads as text on white AND takes white text as a button
    # fill (a pale mint or pure yellow would fail one of those).
    SUCCESS = "#10B981"         # Emerald
    ERROR = "#EF4444"           # Red
    WARNING = "#F59E0B"         # Amber (yellow vanishes on white)

    # Background colors — light
    BG_DARK = "#F4F4F9"         # Page background (light lavender-grey)
    BG_CARD = "#FFFFFF"         # Card / panel background
    BG_CARD_HOVER = "#EFEFF5"   # Card hover
    BG_INPUT = "#F1F1F7"        # Input field background
    BG_BUTTON = "#E9E9F1"       # Default / secondary button fill

    # Text colors
    TEXT = "#2D2D3A"            # Primary text (dark navy on light)
    TEXT_MUTED = "#8A8A9C"      # Secondary text
    # Historical name — now means "text drawn ON an accent-colored fill".
    # All accent fills (violet/emerald/red/rose/amber) take white text.
    TEXT_DARK = "#FFFFFF"

    # Border colors
    BORDER = "#E3E3EC"          # Default border (light grey)
    BORDER_LIGHT = "#EDEDF3"    # Lighter border
    BORDER_PRIMARY = "#7C5CFC"  # Violet border (focused)

    # OLED Preview colors (physical screen — keep black/cyan for accuracy)
    OLED_BG = "#000000"
    OLED_FG = "#00FFFF"
    OLED_HIGHLIGHT = "#FFFF00"


# ============================================================
# FONT CONFIGURATION
# ============================================================
class Fonts:
    # Primary font (fallback to Segoe UI on Windows)
    FAMILY = "Segoe UI"
    MONO = "Consolas"

    # Sizes (larger for better readability)
    SIZE_TITLE = 16
    SIZE_HEADING = 13
    SIZE_NORMAL = 11
    SIZE_SMALL = 10

    @classmethod
    def title(cls):
        return (cls.FAMILY, cls.SIZE_TITLE, "bold")

    @classmethod
    def heading(cls):
        return (cls.FAMILY, cls.SIZE_HEADING, "bold")

    @classmethod
    def normal(cls):
        return (cls.FAMILY, cls.SIZE_NORMAL)

    @classmethod
    def bold(cls):
        return (cls.FAMILY, cls.SIZE_NORMAL, "bold")

    @classmethod
    def small(cls):
        return (cls.FAMILY, cls.SIZE_SMALL)

    @classmethod
    def mono(cls):
        return (cls.MONO, cls.SIZE_NORMAL)

    @classmethod
    def mono_small(cls):
        return (cls.MONO, cls.SIZE_SMALL)


# ============================================================
# TTK STYLE CONFIGURATION
# ============================================================
def apply_theme(root: tk.Tk):
    """Apply the FlashPorter light theme to the application."""

    style = ttk.Style()

    # Use 'clam' as base (most customizable)
    style.theme_use('clam')

    # ==================== GENERAL ====================
    style.configure(".",
        background=Colors.BG_DARK,
        foreground=Colors.TEXT,
        fieldbackground=Colors.BG_INPUT,
        font=Fonts.normal(),
        borderwidth=0,
        focuscolor=Colors.PRIMARY
    )

    # ==================== FRAMES ====================
    style.configure("TFrame",
        background=Colors.BG_DARK
    )

    style.configure("Card.TFrame",
        background=Colors.BG_CARD,
        relief="flat"
    )

    # ==================== LABELS ====================
    style.configure("TLabel",
        background=Colors.BG_DARK,
        foreground=Colors.TEXT,
        font=Fonts.normal()
    )

    style.configure("Title.TLabel",
        background=Colors.BG_DARK,
        foreground=Colors.PRIMARY,
        font=Fonts.title()
    )

    style.configure("Heading.TLabel",
        background=Colors.BG_DARK,
        foreground=Colors.TEXT,
        font=Fonts.heading()
    )

    style.configure("Muted.TLabel",
        background=Colors.BG_DARK,
        foreground=Colors.TEXT_MUTED,
        font=Fonts.small()
    )

    style.configure("Success.TLabel",
        background=Colors.BG_DARK,
        foreground=Colors.SUCCESS,
        font=Fonts.normal()
    )

    style.configure("Error.TLabel",
        background=Colors.BG_DARK,
        foreground=Colors.ERROR,
        font=Fonts.normal()
    )

    style.configure("Card.TLabel",
        background=Colors.BG_CARD,
        foreground=Colors.TEXT
    )

    style.configure("Section.TLabel",
        background=Colors.BG_CARD,
        foreground=Colors.PRIMARY,
        font=(Fonts.FAMILY, 11, "bold")
    )

    # ==================== LABELFRAME ====================
    style.configure("TLabelframe",
        background=Colors.BG_CARD,
        foreground=Colors.TEXT,
        bordercolor=Colors.BORDER,
        relief="flat",
        borderwidth=1
    )

    style.configure("TLabelframe.Label",
        background=Colors.BG_CARD,
        foreground=Colors.PRIMARY,
        font=(Fonts.FAMILY, 12, "bold")
    )

    # ==================== BUTTONS ====================

    # Default button (dark, subtle border)
    style.configure("TButton",
        background=Colors.BG_BUTTON,
        foreground=Colors.TEXT,
        font=Fonts.normal(),
        padding=(12, 6),
        borderwidth=1,
        bordercolor=Colors.BORDER_LIGHT,
        relief="flat"
    )
    style.map("TButton",
        background=[
            ("disabled", Colors.BG_DARK),
            ("active", Colors.BG_CARD_HOVER),
            ("pressed", Colors.BG_CARD)
        ],
        foreground=[
            ("disabled", Colors.TEXT_MUTED),
            ("active", Colors.PRIMARY)
        ]
    )

    # Primary button (cyan)
    style.configure("Primary.TButton",
        background=Colors.PRIMARY,
        foreground=Colors.TEXT_DARK,
        font=Fonts.bold(),
        padding=(16, 8),
        borderwidth=0
    )
    style.map("Primary.TButton",
        background=[
            ("disabled", Colors.BG_BUTTON),
            ("active", Colors.PRIMARY_LIGHT),
            ("pressed", Colors.PRIMARY_DARK)
        ],
        foreground=[
            ("disabled", Colors.TEXT_MUTED),
            ("active", Colors.TEXT_DARK),
            ("pressed", Colors.TEXT_DARK)
        ]
    )

    # Secondary button (outlined)
    style.configure("Secondary.TButton",
        background=Colors.BG_BUTTON,
        foreground=Colors.PRIMARY,
        font=Fonts.normal(),
        padding=(14, 7),
        borderwidth=1,
        bordercolor=Colors.PRIMARY_DARK
    )
    style.map("Secondary.TButton",
        background=[
            ("disabled", Colors.BG_DARK),
            ("active", Colors.BG_CARD_HOVER),
            ("pressed", Colors.BG_CARD)
        ],
        foreground=[
            ("disabled", Colors.TEXT_MUTED),
            ("active", Colors.PRIMARY_LIGHT)
        ]
    )

    # Success button (green)
    style.configure("Success.TButton",
        background=Colors.SUCCESS,
        foreground=Colors.TEXT_DARK,
        font=Fonts.bold(),
        padding=(16, 8),
        borderwidth=0
    )
    style.map("Success.TButton",
        background=[
            ("disabled", Colors.BG_BUTTON),
            ("active", "#34D399"),
            ("pressed", "#059669")
        ],
        foreground=[
            ("disabled", Colors.TEXT_MUTED)
        ]
    )

    # Accent button (orange)
    style.configure("Accent.TButton",
        background=Colors.ACCENT,
        foreground="#FFFFFF",
        font=Fonts.bold(),
        padding=(16, 8),
        borderwidth=0
    )
    style.map("Accent.TButton",
        background=[
            ("disabled", Colors.BG_BUTTON),
            ("active", Colors.ACCENT_LIGHT),
            ("pressed", "#DB2777")
        ],
        foreground=[
            ("disabled", Colors.TEXT_MUTED)
        ]
    )

    # Danger button (red)
    style.configure("Danger.TButton",
        background=Colors.ERROR,
        foreground="#FFFFFF",
        font=Fonts.bold(),
        padding=(14, 7),
        borderwidth=0
    )
    style.map("Danger.TButton",
        background=[
            ("disabled", Colors.BG_BUTTON),
            ("active", "#F87171"),
            ("pressed", "#DC2626")
        ],
        foreground=[
            ("disabled", Colors.TEXT_MUTED)
        ]
    )

    # Small icon button (for ↻, ✕, 📋 etc.)
    style.configure("Icon.TButton",
        background=Colors.BG_BUTTON,
        foreground=Colors.TEXT_MUTED,
        font=(Fonts.FAMILY, 9),
        padding=(4, 3),
        borderwidth=1,
        bordercolor=Colors.BORDER
    )
    style.map("Icon.TButton",
        background=[
            ("active", Colors.BG_CARD_HOVER),
            ("pressed", Colors.BG_CARD)
        ],
        foreground=[
            ("active", Colors.PRIMARY)
        ]
    )

    # Small danger icon button (for ✕ remove etc.)
    style.configure("IconDanger.TButton",
        background=Colors.BG_BUTTON,
        foreground=Colors.ERROR,
        font=(Fonts.FAMILY, 9),
        padding=(4, 3),
        borderwidth=1,
        bordercolor=Colors.BORDER
    )
    style.map("IconDanger.TButton",
        background=[
            ("active", "#FCE4E4"),
            ("pressed", Colors.BG_CARD)
        ],
        foreground=[
            ("active", "#DC2626")
        ]
    )

    # ==================== ENTRY ====================
    style.configure("TEntry",
        fieldbackground=Colors.BG_INPUT,
        foreground=Colors.TEXT,
        insertcolor=Colors.TEXT,
        bordercolor=Colors.BORDER,
        lightcolor=Colors.BORDER,
        darkcolor=Colors.BORDER,
        padding=8
    )
    style.map("TEntry",
        bordercolor=[
            ("focus", Colors.PRIMARY)
        ],
        lightcolor=[
            ("focus", Colors.PRIMARY)
        ]
    )

    # ==================== COMBOBOX ====================
    style.configure("TCombobox",
        fieldbackground=Colors.BG_INPUT,
        background=Colors.BG_BUTTON,
        foreground=Colors.TEXT,
        arrowcolor=Colors.TEXT,
        bordercolor=Colors.BORDER,
        lightcolor=Colors.BORDER,
        darkcolor=Colors.BORDER,
        padding=8
    )
    style.map("TCombobox",
        fieldbackground=[
            ("readonly", Colors.BG_INPUT)
        ],
        foreground=[
            ("readonly", Colors.TEXT)
        ],
        bordercolor=[
            ("focus", Colors.PRIMARY)
        ]
    )

    # Combobox dropdown
    root.option_add('*TCombobox*Listbox.background', Colors.BG_CARD)
    root.option_add('*TCombobox*Listbox.foreground', Colors.TEXT)
    root.option_add('*TCombobox*Listbox.selectBackground', Colors.PRIMARY)
    root.option_add('*TCombobox*Listbox.selectForeground', Colors.TEXT_DARK)

    # ==================== NOTEBOOK (TABS) ====================
    style.configure("TNotebook",
        background=Colors.BG_DARK,
        borderwidth=0,
        tabmargins=(2, 4, 2, 0)
    )

    style.configure("TNotebook.Tab",
        background=Colors.BG_CARD,
        foreground=Colors.TEXT_MUTED,
        padding=(20, 10),
        font=(Fonts.FAMILY, Fonts.SIZE_NORMAL, "bold"),
        borderwidth=0,
        focuscolor=Colors.BG_DARK
    )
    style.map("TNotebook.Tab",
        background=[
            ("selected", Colors.BG_DARK),
            ("active", Colors.BG_CARD_HOVER)
        ],
        foreground=[
            ("selected", Colors.PRIMARY),
            ("active", Colors.TEXT)
        ],
        padding=[
            ("selected", (20, 10, 20, 12))
        ]
    )

    # Notebook tab area border (cyan line under selected tab)
    style.layout("TNotebook", [
        ("TNotebook.client", {"sticky": "nswe"})
    ])
    style.layout("TNotebook.Tab", [
        ("Notebook.tab", {"sticky": "nswe", "children": [
            ("Notebook.padding", {"side": "top", "sticky": "nswe", "children": [
                ("Notebook.label", {"side": "top", "sticky": ""})
            ]})
        ]})
    ])

    # ==================== TREEVIEW ====================
    style.configure("Treeview",
        background=Colors.BG_CARD,
        foreground=Colors.TEXT,
        fieldbackground=Colors.BG_CARD,
        bordercolor=Colors.BORDER,
        font=(Fonts.FAMILY, Fonts.SIZE_SMALL),
        rowheight=28
    )
    style.configure("Treeview.Heading",
        background=Colors.BG_BUTTON,
        foreground=Colors.PRIMARY,
        font=(Fonts.FAMILY, Fonts.SIZE_SMALL, "bold"),
        borderwidth=0,
        relief="flat",
        padding=(8, 4)
    )
    style.map("Treeview",
        background=[
            ("selected", Colors.PRIMARY)
        ],
        foreground=[
            ("selected", Colors.TEXT_DARK)
        ]
    )
    style.map("Treeview.Heading",
        background=[
            ("active", Colors.BG_CARD_HOVER)
        ],
        foreground=[
            ("active", Colors.PRIMARY_LIGHT)
        ]
    )

    # ==================== SCROLLBAR ====================
    style.configure("Vertical.TScrollbar",
        background=Colors.BG_CARD,
        troughcolor=Colors.BG_DARK,
        bordercolor=Colors.BG_DARK,
        arrowcolor=Colors.TEXT_MUTED,
        width=12
    )
    style.map("Vertical.TScrollbar",
        background=[
            ("active", Colors.BG_CARD_HOVER),
            ("pressed", Colors.PRIMARY_DARK)
        ]
    )

    style.configure("Horizontal.TScrollbar",
        background=Colors.BG_CARD,
        troughcolor=Colors.BG_DARK,
        bordercolor=Colors.BG_DARK,
        arrowcolor=Colors.TEXT_MUTED,
        width=12
    )

    # ==================== PROGRESSBAR ====================
    style.configure("TProgressbar",
        background=Colors.PRIMARY,
        troughcolor=Colors.BG_INPUT,
        bordercolor=Colors.BORDER,
        lightcolor=Colors.PRIMARY,
        darkcolor=Colors.PRIMARY_DARK
    )

    style.configure("Green.TProgressbar",
        background=Colors.SUCCESS
    )

    style.configure("Orange.TProgressbar",
        background=Colors.ACCENT
    )

    # ==================== SEPARATOR ====================
    style.configure("TSeparator",
        background=Colors.BORDER
    )

    # ==================== CHECKBUTTON ====================
    style.configure("TCheckbutton",
        background=Colors.BG_DARK,
        foreground=Colors.TEXT,
        indicatorbackground=Colors.BG_INPUT,
        indicatorforeground=Colors.PRIMARY
    )
    style.map("TCheckbutton",
        background=[
            ("active", Colors.BG_DARK)
        ],
        indicatorbackground=[
            ("selected", Colors.PRIMARY)
        ]
    )

    # ==================== RADIOBUTTON ====================
    style.configure("TRadiobutton",
        background=Colors.BG_DARK,
        foreground=Colors.TEXT,
        indicatorbackground=Colors.BG_INPUT
    )
    style.map("TRadiobutton",
        indicatorbackground=[
            ("selected", Colors.PRIMARY)
        ]
    )

    # ==================== SCALE ====================
    style.configure("TScale",
        background=Colors.BG_DARK,
        troughcolor=Colors.BG_INPUT,
        sliderthickness=16
    )

    # ==================== PANEDWINDOW ====================
    style.configure("TPanedwindow",
        background=Colors.BG_DARK
    )

    # Configure root window
    root.configure(bg=Colors.BG_DARK)

    return style


# ============================================================
# CUSTOM WIDGETS
# ============================================================

class GradientFrame(tk.Canvas):
    """A frame with gradient background effect."""

    def __init__(self, parent, color1=Colors.PRIMARY, color2=Colors.SUCCESS, **kwargs):
        super().__init__(parent, highlightthickness=0, **kwargs)
        self.color1 = color1
        self.color2 = color2
        self.bind("<Configure>", self._draw_gradient)

    def _draw_gradient(self, event=None):
        """Draw gradient on canvas."""
        self.delete("gradient")
        width = self.winfo_width()
        height = self.winfo_height()

        # Simple horizontal gradient
        limit = width
        r1, g1, b1 = self.winfo_rgb(self.color1)
        r2, g2, b2 = self.winfo_rgb(self.color2)

        r_ratio = (r2 - r1) / limit
        g_ratio = (g2 - g1) / limit
        b_ratio = (b2 - b1) / limit

        for i in range(limit):
            nr = int(r1 + (r_ratio * i))
            ng = int(g1 + (g_ratio * i))
            nb = int(b1 + (b_ratio * i))
            color = f"#{nr:04x}{ng:04x}{nb:04x}"
            self.create_line(i, 0, i, height, tags=("gradient",), fill=color)


class ModernButton(tk.Canvas):
    """Modern button with hover effects and rounded corners."""

    def __init__(self, parent, text="Button", command=None,
                 bg=Colors.PRIMARY, fg=Colors.TEXT_DARK,
                 hover_bg=Colors.PRIMARY_LIGHT,
                 width=120, height=36, radius=8, **kwargs):
        super().__init__(parent, width=width, height=height,
                        highlightthickness=0, bg=Colors.BG_DARK, **kwargs)

        self.text = text
        self.command = command
        self.bg = bg
        self.fg = fg
        self.hover_bg = hover_bg
        self.radius = radius
        self._pressed = False

        self._draw()

        self.bind("<Enter>", self._on_enter)
        self.bind("<Leave>", self._on_leave)
        self.bind("<Button-1>", self._on_press)
        self.bind("<ButtonRelease-1>", self._on_release)

    def _draw(self, bg=None):
        """Draw button with rounded corners."""
        self.delete("all")
        bg = bg or self.bg

        w = self.winfo_reqwidth()
        h = self.winfo_reqheight()
        r = self.radius

        # Rounded rectangle
        self.create_arc(0, 0, r*2, r*2, start=90, extent=90, fill=bg, outline=bg)
        self.create_arc(w-r*2, 0, w, r*2, start=0, extent=90, fill=bg, outline=bg)
        self.create_arc(0, h-r*2, r*2, h, start=180, extent=90, fill=bg, outline=bg)
        self.create_arc(w-r*2, h-r*2, w, h, start=270, extent=90, fill=bg, outline=bg)
        self.create_rectangle(r, 0, w-r, h, fill=bg, outline=bg)
        self.create_rectangle(0, r, w, h-r, fill=bg, outline=bg)

        # Text
        self.create_text(w/2, h/2, text=self.text, fill=self.fg, font=Fonts.bold())

    def _on_enter(self, event):
        self._draw(self.hover_bg)

    def _on_leave(self, event):
        self._draw(self.bg)

    def _on_press(self, event):
        self._pressed = True

    def _on_release(self, event):
        if self._pressed and self.command:
            self.command()
        self._pressed = False


class StatusBadge(tk.Label):
    """A colored badge for status display."""

    def __init__(self, parent, text="Status", status="info", **kwargs):
        super().__init__(parent, **kwargs)

        self.status_colors = {
            "info": (Colors.PRIMARY, Colors.TEXT_DARK),
            "success": (Colors.SUCCESS, Colors.TEXT_DARK),
            "warning": (Colors.WARNING, Colors.TEXT_DARK),
            "error": (Colors.ERROR, Colors.TEXT),
            "muted": (Colors.BG_CARD_HOVER, Colors.TEXT_MUTED)
        }

        self.configure(
            text=text,
            font=Fonts.small(),
            padx=12,
            pady=4
        )
        self.set_status(status)

    def set_status(self, status):
        """Update badge status."""
        bg, fg = self.status_colors.get(status, self.status_colors["info"])
        self.configure(bg=bg, fg=fg)

    def set_text(self, text):
        """Update badge text."""
        self.configure(text=text)


# ============================================================
# UTILITY FUNCTIONS
# ============================================================

def create_card_frame(parent, title=None, padding=15):
    """Create a card-style frame with optional title."""
    if title:
        frame = ttk.LabelFrame(parent, text=title, padding=padding)
    else:
        frame = ttk.Frame(parent, style="Card.TFrame", padding=padding)
    return frame


def configure_grid_weights(frame, rows=None, cols=None):
    """Configure grid row/column weights."""
    if rows:
        for i, weight in enumerate(rows):
            frame.rowconfigure(i, weight=weight)
    if cols:
        for i, weight in enumerate(cols):
            frame.columnconfigure(i, weight=weight)
