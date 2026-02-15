"""
oled_preview.py - OLED Display Simulator
=========================================
Simulates SH1106G/SSD1306 128x64 OLED display.
Shows firmware list exactly as it appears on hardware.
Supports drag-and-drop reordering.
"""

import tkinter as tk
from tkinter import ttk
from typing import List, Callable, Optional

# Import theme colors for UI elements (not OLED simulation)
try:
    from .theme import Colors, Fonts
except ImportError:
    # Fallback for standalone testing
    class Colors:
        BG_DARK = "#121620"
        BG_CARD = "#1A202C"
        PRIMARY = "#00D9FF"
        TEXT = "#C8CDD8"
        TEXT_MUTED = "#7A849A"
        TEXT_DARK = "#121620"
        BORDER = "#2A3445"
        ERROR = "#FF4757"

    class Fonts:
        @staticmethod
        def normal(): return ("Segoe UI", 11)
        @staticmethod
        def heading(): return ("Segoe UI", 13, "bold")
        @staticmethod
        def mono(): return ("Consolas", 11)
        @staticmethod
        def mono_small(): return ("Consolas", 10)
        @staticmethod
        def bold(): return ("Segoe UI", 11, "bold")

# OLED Constants (match hardware)
OLED_WIDTH = 128
OLED_HEIGHT = 64
CHAR_WIDTH = 6
CHAR_HEIGHT = 8
MAX_VISIBLE = 7  # Max items visible at once
HEADER_HEIGHT = 10  # Tab header height

# Colors (simulating OLED - keep original for accuracy)
COLOR_BG = "#000000"  # Black background
COLOR_FG = "#00FFFF"  # Cyan/Blue (typical OLED color)
COLOR_SELECTED = "#FFFFFF"  # White for selected item
COLOR_HEADER = "#FFFF00"  # Yellow for header


class OLEDPreview(tk.Canvas):
    """
    Canvas widget that simulates OLED display.
    Shows firmware list with selection highlight.
    """

    # Scale factor for display (make it bigger on PC)
    SCALE = 3

    def __init__(self, parent, **kwargs):
        # Calculate actual canvas size
        width = OLED_WIDTH * self.SCALE
        height = OLED_HEIGHT * self.SCALE

        super().__init__(
            parent,
            width=width,
            height=height,
            bg=COLOR_BG,
            highlightthickness=2,
            highlightbackground=Colors.PRIMARY,  # Cyan border to match theme
            **kwargs
        )

        self.items: List[str] = []
        self.selected_index = 0
        self.scroll_offset = 0
        self.header_text = "FIRMWARE"
        self.show_header = True

        # Drag and drop state
        self.drag_index: Optional[int] = None
        self.on_reorder: Optional[Callable[[int, int], None]] = None

        # Bind mouse events for drag-drop
        self.bind("<Button-1>", self._on_click)
        self.bind("<B1-Motion>", self._on_drag)
        self.bind("<ButtonRelease-1>", self._on_drop)

        # Bind mouse wheel for scrolling
        self.bind("<MouseWheel>", self._on_mousewheel)  # Windows
        self.bind("<Button-4>", lambda e: self._scroll(-1))  # Linux scroll up
        self.bind("<Button-5>", lambda e: self._scroll(1))   # Linux scroll down

        # On Windows, capture mouse wheel when mouse enters the canvas
        self.bind("<Enter>", self._on_enter)
        self.bind("<Leave>", self._on_leave)
        self._mouse_over = False

        # Bind keyboard for navigation preview
        self.bind("<Up>", lambda e: self.move_selection(-1))
        self.bind("<Down>", lambda e: self.move_selection(1))
        self.focus_set()

    def set_items(self, items: List[str], selected: int = 0):
        """Set list of items to display."""
        self.items = items.copy()
        self.selected_index = max(0, min(selected, len(items) - 1))
        self._update_scroll()
        self._redraw()

    def set_header(self, text: str, show: bool = True):
        """Set header text."""
        self.header_text = text
        self.show_header = show
        self._redraw()

    def get_items(self) -> List[str]:
        """Get current items list."""
        return self.items.copy()

    def get_selected(self) -> int:
        """Get selected index."""
        return self.selected_index

    def move_selection(self, delta: int):
        """Move selection up or down."""
        if not self.items:
            return

        self.selected_index = max(0, min(
            self.selected_index + delta,
            len(self.items) - 1
        ))
        self._update_scroll()
        self._redraw()

    def _on_enter(self, event):
        """Mouse entered canvas - capture scroll events."""
        self._mouse_over = True
        # Bind to root window to capture mouse wheel regardless of focus
        self._toplevel = self.winfo_toplevel()
        self._toplevel.bind("<MouseWheel>", self._on_mousewheel_global)

    def _on_leave(self, event):
        """Mouse left canvas - release scroll events."""
        self._mouse_over = False
        try:
            self._toplevel.unbind("<MouseWheel>")
        except:
            pass

    def _on_mousewheel_global(self, event):
        """Handle mouse wheel from root window."""
        if self._mouse_over:
            self._on_mousewheel(event)
            return "break"  # Prevent event propagation

    def _on_mousewheel(self, event):
        """Handle mouse wheel scrolling (Windows)."""
        # event.delta is positive for scroll up, negative for scroll down
        delta = -1 if event.delta > 0 else 1
        self._scroll(delta)
        return "break"  # Prevent event propagation

    def _scroll(self, delta: int):
        """Scroll the view by delta items."""
        if not self.items:
            return

        visible = MAX_VISIBLE if not self.show_header else MAX_VISIBLE - 1
        max_scroll = max(0, len(self.items) - visible)

        self.scroll_offset = max(0, min(self.scroll_offset + delta, max_scroll))
        self._redraw()

    def set_reorder_callback(self, callback: Callable[[int, int], None]):
        """Set callback when items are reordered. callback(from_idx, to_idx)"""
        self.on_reorder = callback

    def _update_scroll(self):
        """Update scroll offset to keep selection visible."""
        visible = MAX_VISIBLE if not self.show_header else MAX_VISIBLE - 1

        if self.selected_index < self.scroll_offset:
            self.scroll_offset = self.selected_index
        elif self.selected_index >= self.scroll_offset + visible:
            self.scroll_offset = self.selected_index - visible + 1

    def _redraw(self):
        """Redraw the OLED display."""
        self.delete("all")

        s = self.SCALE
        y = 0

        # Draw header
        if self.show_header:
            # Header background
            self.create_rectangle(
                0, 0,
                OLED_WIDTH * s, HEADER_HEIGHT * s,
                fill="#111111", outline=""
            )
            # Header text (centered)
            self._draw_text(
                OLED_WIDTH // 2,
                1,
                self.header_text,
                COLOR_HEADER,
                center=True
            )
            y = HEADER_HEIGHT

        # Draw items
        visible = MAX_VISIBLE if not self.show_header else MAX_VISIBLE - 1

        for i in range(visible):
            item_idx = self.scroll_offset + i
            if item_idx >= len(self.items):
                break

            item_text = self.items[item_idx]
            item_y = y + i * CHAR_HEIGHT

            # Selection highlight
            is_selected = (item_idx == self.selected_index)
            if is_selected:
                self.create_rectangle(
                    0, item_y * s,
                    OLED_WIDTH * s, (item_y + CHAR_HEIGHT) * s,
                    fill=COLOR_FG, outline=""
                )

            # Draw text with index number (1-based)
            color = COLOR_BG if is_selected else COLOR_FG
            # Format: "01.Name" - index takes 3 chars, rest for name (18 chars)
            idx_str = f"{item_idx + 1:02d}."
            name_text = item_text[:18]  # Leave room for index
            display_text = f"{idx_str}{name_text}"
            self._draw_text(2, item_y, display_text, color)

            # Show drag indicator
            if self.drag_index == item_idx:
                self.create_rectangle(
                    0, item_y * s,
                    OLED_WIDTH * s, (item_y + CHAR_HEIGHT) * s,
                    fill="", outline="#FF0000", width=2
                )

        # Scrollbar indicator (if needed)
        if len(self.items) > visible:
            self._draw_scrollbar(visible)

    def _draw_text(self, x: int, y: int, text: str, color: str, center: bool = False):
        """Draw text at OLED coordinates."""
        s = self.SCALE
        font_size = int(CHAR_HEIGHT * s * 0.8)

        if center:
            self.create_text(
                x * s, y * s + font_size // 2,
                text=text,
                fill=color,
                font=("Consolas", font_size, "bold"),
                anchor="center"
            )
        else:
            self.create_text(
                x * s, y * s,
                text=text,
                fill=color,
                font=("Consolas", font_size),
                anchor="nw"
            )

    def _draw_scrollbar(self, visible: int):
        """Draw scrollbar indicator on right side."""
        s = self.SCALE
        total = len(self.items)

        # Scrollbar track
        track_x = (OLED_WIDTH - 3) * s
        track_y = HEADER_HEIGHT * s if self.show_header else 0
        track_h = (OLED_HEIGHT - (HEADER_HEIGHT if self.show_header else 0)) * s

        # Scrollbar thumb
        thumb_h = max(10, track_h * visible // total)
        thumb_y = track_y + (track_h - thumb_h) * self.scroll_offset // max(1, total - visible)

        self.create_rectangle(
            track_x, thumb_y,
            track_x + 2 * s, thumb_y + thumb_h,
            fill=COLOR_FG, outline=""
        )

    def _get_item_at_y(self, y: int) -> int:
        """Get item index at screen Y coordinate."""
        s = self.SCALE
        header_offset = HEADER_HEIGHT if self.show_header else 0

        item_y = (y // s - header_offset) // CHAR_HEIGHT
        return self.scroll_offset + item_y

    def _on_click(self, event):
        """Handle mouse click."""
        self.focus_set()
        idx = self._get_item_at_y(event.y)
        if 0 <= idx < len(self.items):
            self.selected_index = idx
            self.drag_index = idx
            self._redraw()

    def _on_drag(self, event):
        """Handle mouse drag."""
        if self.drag_index is None:
            return

        idx = self._get_item_at_y(event.y)
        if 0 <= idx < len(self.items) and idx != self.selected_index:
            self.selected_index = idx
            self._redraw()

    def _on_drop(self, event):
        """Handle mouse release (drop)."""
        if self.drag_index is None:
            return

        drop_idx = self._get_item_at_y(event.y)
        drop_idx = max(0, min(drop_idx, len(self.items) - 1))

        if drop_idx != self.drag_index and self.on_reorder:
            # Reorder items
            item = self.items.pop(self.drag_index)
            self.items.insert(drop_idx, item)
            self.selected_index = drop_idx
            self.on_reorder(self.drag_index, drop_idx)

        self.drag_index = None
        self._redraw()


class OLEDPreviewFrame(ttk.LabelFrame):
    """
    Complete frame with OLED preview and control buttons.
    Supports reordering and renaming items.
    """

    def __init__(self, parent, **kwargs):
        super().__init__(parent, text="OLED Preview (Click to select)", **kwargs)

        # OLED display
        self.oled = OLEDPreview(self)
        self.oled.pack(pady=10, padx=10)
        self.oled.set_reorder_callback(self._on_reorder)

        # Control buttons row 1: Move
        btn_frame1 = ttk.Frame(self)
        btn_frame1.pack(fill=tk.X, padx=10, pady=(0, 5))

        ttk.Button(btn_frame1, text="Up", width=6, command=self._move_up).pack(side=tk.LEFT, padx=2)
        ttk.Button(btn_frame1, text="Down", width=6, command=self._move_down).pack(side=tk.LEFT, padx=2)
        ttk.Button(btn_frame1, text="Rename", width=8, command=self._rename_item).pack(side=tk.LEFT, padx=2)

        # Control buttons row 2: Apply
        btn_frame2 = ttk.Frame(self)
        btn_frame2.pack(fill=tk.X, padx=10, pady=(0, 10))

        ttk.Button(btn_frame2, text="Apply Order", style="Primary.TButton", command=self._apply_order).pack(fill=tk.X)

        # Callback when order is applied
        self.on_apply: Optional[Callable[[List[str]], None]] = None

        # Callback when item is renamed (fw_id, device_type, version)
        self.on_rename: Optional[Callable[[str, str, str], None]] = None

        # Store original fw_ids mapping
        self.fw_id_map: dict = {}  # display_name -> fw_id

    def set_items(self, items, header: str = "FIRMWARE"):
        """
        Set items to preview.

        Args:
            items: List of tuples (fw_id, display_name) or just strings
            header: Header text for OLED
        """
        self.oled.set_header(header)
        self.fw_id_map = {}

        display_names = []
        for item in items:
            if isinstance(item, tuple) and len(item) == 2:
                # New format: (fw_id, display_name)
                fw_id, display_name = item
                self.fw_id_map[display_name] = fw_id
                display_names.append(display_name)
            else:
                # Old format: just string (assume fw_id is first word)
                parts = str(item).split()
                fw_id = parts[0] if parts else str(item)
                self.fw_id_map[str(item)] = fw_id
                display_names.append(str(item))

        self.oled.set_items(display_names)

    def get_items(self) -> List[str]:
        """Get current order."""
        return self.oled.get_items()

    def get_fw_ids(self) -> List[str]:
        """Get firmware IDs in current order."""
        result = []
        for item in self.oled.get_items():
            fw_id = self.fw_id_map.get(item, item)  # Default to display_name if not found
            result.append(fw_id)
        return result

    def set_apply_callback(self, callback: Callable[[List[str]], None]):
        """Set callback when Apply Order is clicked."""
        self.on_apply = callback

    def set_rename_callback(self, callback: Callable[[str, str, str], None]):
        """Set callback when item is renamed. callback(fw_id, device_type, version)"""
        self.on_rename = callback

    def _on_reorder(self, from_idx: int, to_idx: int):
        """Called when items are reordered via drag-drop."""
        pass  # Just update display, actual save on Apply

    def _move_up(self):
        """Move selected item up."""
        idx = self.oled.get_selected()
        if idx > 0:
            items = self.oled.get_items()
            items[idx], items[idx - 1] = items[idx - 1], items[idx]
            self.oled.set_items(items, idx - 1)

    def _move_down(self):
        """Move selected item down."""
        idx = self.oled.get_selected()
        items = self.oled.get_items()
        if idx < len(items) - 1:
            items[idx], items[idx + 1] = items[idx + 1], items[idx]
            self.oled.set_items(items, idx + 1)

    def _rename_item(self):
        """Rename selected item - edits device_type and version in metadata."""
        idx = self.oled.get_selected()
        items = self.oled.get_items()

        if not items or idx >= len(items):
            return

        old_name = items[idx]
        fw_id = self.fw_id_map.get(old_name, old_name)

        # Parse current display name to get device_type and version
        parts = old_name.split()
        current_device = parts[0] if len(parts) >= 1 else ""
        current_version = parts[1] if len(parts) >= 2 else ""

        # Show rename dialog with dark theme
        dialog = tk.Toplevel(self)
        dialog.title("Edit Firmware Display")
        dialog.geometry("400x240")
        dialog.resizable(False, False)
        dialog.configure(bg=Colors.BG_DARK)
        dialog.transient(self)
        dialog.grab_set()

        # Center
        dialog.update_idletasks()
        x = self.winfo_rootx() + 50
        y = self.winfo_rooty() + 50
        dialog.geometry(f"+{x}+{y}")

        frm = ttk.Frame(dialog, padding=20)
        frm.pack(fill=tk.BOTH, expand=True)
        frm.columnconfigure(1, weight=1)

        # Title
        tk.Label(
            frm,
            text="Edit Firmware Display",
            font=Fonts.heading(),
            fg=Colors.PRIMARY,
            bg=Colors.BG_DARK
        ).grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 15))

        # Firmware ID (readonly info)
        tk.Label(
            frm,
            text=f"Firmware ID: {fw_id}",
            font=Fonts.mono_small(),
            fg=Colors.TEXT_MUTED,
            bg=Colors.BG_DARK
        ).grid(row=1, column=0, columnspan=2, sticky="w", pady=(0, 10))

        ttk.Label(frm, text="Device Type:").grid(row=2, column=0, sticky="w", pady=5)
        device_var = tk.StringVar(value=current_device)
        entry_device = ttk.Entry(frm, textvariable=device_var, font=Fonts.mono())
        entry_device.grid(row=2, column=1, sticky="ew", padx=(10, 0), pady=5)

        ttk.Label(frm, text="Version:").grid(row=3, column=0, sticky="w", pady=5)
        version_var = tk.StringVar(value=current_version)
        entry_version = ttk.Entry(frm, textvariable=version_var, font=Fonts.mono())
        entry_version.grid(row=3, column=1, sticky="ew", padx=(10, 0), pady=5)

        # Preview with cyan color
        preview_var = tk.StringVar()

        def update_preview(*args):
            d = device_var.get().strip()
            v = version_var.get().strip()
            preview = f"{d} {v}"[:21]
            preview_var.set(f"OLED Preview: {preview}")

        device_var.trace_add("write", update_preview)
        version_var.trace_add("write", update_preview)
        update_preview()

        tk.Label(
            frm,
            textvariable=preview_var,
            font=Fonts.mono_small(),
            fg=Colors.PRIMARY,
            bg=Colors.BG_DARK
        ).grid(row=4, column=0, columnspan=2, sticky="w", pady=(10, 0))

        entry_device.select_range(0, tk.END)
        entry_device.focus()

        def save():
            new_device = device_var.get().strip()
            new_version = version_var.get().strip()

            if new_device or new_version:
                # Call rename callback to update metadata
                if self.on_rename:
                    self.on_rename(fw_id, new_device, new_version)

                # Update local display immediately
                new_name = f"{new_device} {new_version}"[:21]
                if old_name in self.fw_id_map:
                    self.fw_id_map[new_name] = self.fw_id_map.pop(old_name)
                else:
                    self.fw_id_map[new_name] = fw_id
                items[idx] = new_name
                self.oled.set_items(items, idx)

            dialog.destroy()

        def cancel():
            dialog.destroy()

        btn_frm = ttk.Frame(frm)
        btn_frm.grid(row=5, column=0, columnspan=2, pady=(15, 0))
        ttk.Button(btn_frm, text="Save", style="Primary.TButton", command=save).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frm, text="Cancel", style="Secondary.TButton", command=cancel).pack(side=tk.LEFT)

        entry_device.bind("<Return>", lambda e: entry_version.focus())
        entry_version.bind("<Return>", lambda e: save())
        dialog.bind("<Escape>", lambda e: cancel())

    def _apply_order(self):
        """Apply current order and names."""
        if self.on_apply:
            # Return list of (display_name, fw_id) or just fw_ids
            self.on_apply(self.oled.get_items())


# Quick test
if __name__ == "__main__":
    # Try to import and apply theme
    try:
        from theme import apply_theme
        root = tk.Tk()
        apply_theme(root)
    except ImportError:
        root = tk.Tk()
        root.configure(bg=Colors.BG_DARK)

    root.title("OLED Preview Test")

    preview = OLEDPreviewFrame(root)
    preview.pack(padx=20, pady=20)

    # Test items (fw_id, display_name) format
    test_items = [
        ("FW001", "VMDD v1.0"),
        ("FW002", "GTDD v2.1"),
        ("FW003", "TEST v0.5"),
        ("FW004", "DEMO v1.2"),
        ("FW005", "PROD v3.0"),
        ("FW006", "BETA v0.1"),
        ("FW007", "ALPHA v0.2"),
        ("FW008", "GAMMA v1.5"),
    ]

    preview.set_items(test_items, "FIRMWARE LIST")

    def on_apply(items):
        print("New order:", items)
        print("FW IDs:", preview.get_fw_ids())

    preview.set_apply_callback(on_apply)

    root.mainloop()
