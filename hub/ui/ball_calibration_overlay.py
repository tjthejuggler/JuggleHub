"""
Ball Calibration Overlay

Provides visual overlay on the video feed for ball color calibration.
Shows crosshair, sample markers, and confidence visualization.

Created: 2025-10-03
"""

import logging
from typing import List, Tuple, Optional

try:
    from PyQt6.QtWidgets import QWidget
    from PyQt6.QtCore import Qt, QPoint, QRect
    from PyQt6.QtGui import QPainter, QPen, QBrush, QColor, QFont
    PYQT_AVAILABLE = True
except ImportError:
    PYQT_AVAILABLE = False
    print("⚠️ PyQt6 not available for ball calibration overlay")

logger = logging.getLogger(__name__)


class BallCalibrationOverlay(QWidget):
    """
    Transparent overlay widget for ball calibration visualization.
    
    Features:
    - Crosshair cursor when in calibration mode
    - Sample location markers
    - Confidence visualization (optional)
    - Visual feedback for calibration actions
    """
    
    def __init__(self, parent=None):
        """
        Initialize the calibration overlay.
        
        Args:
            parent: Parent widget (typically the video view)
        """
        super().__init__(parent)
        
        # Make widget transparent and pass-through for mouse events
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, False)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground, True)
        
        # State
        self.calibration_mode = False
        self.mouse_pos = None
        self.sample_markers: List[Tuple[int, int, str]] = []  # (x, y, label)
        self.last_click_pos: Optional[Tuple[int, int]] = None
        self.show_confidence = False
        self.confidence_value = 0.0
        
        # Visual settings
        self.crosshair_size = 30
        self.crosshair_color = QColor(0, 255, 0, 200)  # Green
        self.marker_color = QColor(255, 165, 0, 200)  # Orange
        self.marker_size = 10
        self.flash_duration = 30  # frames
        self.flash_counter = 0
        
        logger.info("BallCalibrationOverlay initialized")
    
    def set_calibration_mode(self, enabled: bool):
        """
        Enable or disable calibration mode.
        
        Args:
            enabled: True to enable calibration mode, False to disable
        """
        self.calibration_mode = enabled
        if not enabled:
            self.mouse_pos = None
        self.update()
        logger.debug(f"Calibration mode: {enabled}")
    
    def set_mouse_position(self, pos: QPoint):
        """
        Update the mouse position for crosshair display.
        
        Args:
            pos: Mouse position in widget coordinates
        """
        if self.calibration_mode:
            self.mouse_pos = pos
            self.update()
    
    def add_sample_marker(self, x: int, y: int, label: str = ""):
        """
        Add a marker at a sample location.
        
        Args:
            x: X coordinate of the sample
            y: Y coordinate of the sample
            label: Optional label for the marker
        """
        self.sample_markers.append((x, y, label))
        self.last_click_pos = (x, y)
        self.flash_counter = self.flash_duration
        self.update()
        logger.debug(f"Added sample marker at ({x}, {y})")
    
    def clear_sample_markers(self):
        """Clear all sample markers."""
        self.sample_markers.clear()
        self.last_click_pos = None
        self.update()
        logger.debug("Cleared all sample markers")
    
    def set_confidence_visualization(self, enabled: bool, confidence: float = 0.0):
        """
        Enable or disable confidence visualization.
        
        Args:
            enabled: True to show confidence, False to hide
            confidence: Confidence value (0.0 to 1.0)
        """
        self.show_confidence = enabled
        self.confidence_value = max(0.0, min(1.0, confidence))
        self.update()
    
    def paintEvent(self, event):
        """Paint the overlay elements."""
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        # Draw sample markers
        self._draw_sample_markers(painter)
        
        # Draw crosshair if in calibration mode
        if self.calibration_mode and self.mouse_pos:
            self._draw_crosshair(painter, self.mouse_pos)
        
        # Draw flash effect for last click
        if self.flash_counter > 0:
            self._draw_flash_effect(painter)
            self.flash_counter -= 1
            if self.flash_counter > 0:
                self.update()  # Continue animation
        
        # Draw confidence visualization if enabled
        if self.show_confidence:
            self._draw_confidence(painter)
    
    def _draw_crosshair(self, painter: QPainter, pos: QPoint):
        """
        Draw crosshair at the given position.
        
        Args:
            painter: QPainter instance
            pos: Position to draw crosshair
        """
        pen = QPen(self.crosshair_color, 2)
        painter.setPen(pen)
        
        x, y = pos.x(), pos.y()
        size = self.crosshair_size
        
        # Horizontal line
        painter.drawLine(x - size, y, x - 5, y)
        painter.drawLine(x + 5, y, x + size, y)
        
        # Vertical line
        painter.drawLine(x, y - size, x, y - 5)
        painter.drawLine(x, y + 5, x, y + size)
        
        # Center circle
        painter.drawEllipse(x - 3, y - 3, 6, 6)
        
        # Instruction text
        painter.setFont(QFont("Arial", 10, QFont.Weight.Bold))
        painter.setPen(QPen(QColor(255, 255, 255, 200)))
        text = "Click on the ball to add color sample"
        text_rect = painter.fontMetrics().boundingRect(text)
        text_x = x - text_rect.width() // 2
        text_y = y + size + 20
        
        # Draw text background
        bg_rect = QRect(text_x - 5, text_y - text_rect.height() - 2, 
                       text_rect.width() + 10, text_rect.height() + 4)
        painter.fillRect(bg_rect, QColor(0, 0, 0, 150))
        
        # Draw text
        painter.drawText(text_x, text_y, text)
    
    def _draw_sample_markers(self, painter: QPainter):
        """
        Draw markers for all sample locations.
        
        Args:
            painter: QPainter instance
        """
        for i, (x, y, label) in enumerate(self.sample_markers):
            # Draw marker circle
            pen = QPen(self.marker_color, 2)
            painter.setPen(pen)
            painter.setBrush(QBrush(QColor(255, 165, 0, 100)))
            painter.drawEllipse(x - self.marker_size, y - self.marker_size, 
                              self.marker_size * 2, self.marker_size * 2)
            
            # Draw marker number
            painter.setFont(QFont("Arial", 8, QFont.Weight.Bold))
            painter.setPen(QPen(QColor(255, 255, 255)))
            painter.drawText(x - 4, y + 4, str(i + 1))
            
            # Draw label if provided
            if label:
                painter.setFont(QFont("Arial", 9))
                painter.drawText(x + self.marker_size + 5, y + 4, label)
    
    def _draw_flash_effect(self, painter: QPainter):
        """
        Draw flash effect at the last click position.
        
        Args:
            painter: QPainter instance
        """
        if not self.last_click_pos:
            return
        
        x, y = self.last_click_pos
        
        # Calculate alpha based on flash counter (fade out)
        alpha = int(255 * (self.flash_counter / self.flash_duration))
        
        # Draw expanding circle
        radius = int(self.marker_size * (1 + (self.flash_duration - self.flash_counter) / self.flash_duration))
        
        pen = QPen(QColor(0, 255, 0, alpha), 3)
        painter.setPen(pen)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawEllipse(x - radius, y - radius, radius * 2, radius * 2)
        
        # Draw success checkmark
        if self.flash_counter > self.flash_duration * 0.7:
            painter.setFont(QFont("Arial", 16, QFont.Weight.Bold))
            painter.setPen(QPen(QColor(0, 255, 0, alpha)))
            painter.drawText(x - 8, y + 6, "✓")
    
    def _draw_confidence(self, painter: QPainter):
        """
        Draw confidence visualization.
        
        Args:
            painter: QPainter instance
        """
        # Draw confidence bar in top-right corner
        bar_width = 200
        bar_height = 20
        margin = 10
        
        x = self.width() - bar_width - margin
        y = margin
        
        # Background
        painter.fillRect(x, y, bar_width, bar_height, QColor(0, 0, 0, 150))
        
        # Confidence bar
        confidence_width = int(bar_width * self.confidence_value)
        
        # Color based on confidence level
        if self.confidence_value >= 0.7:
            color = QColor(76, 175, 80)  # Green
        elif self.confidence_value >= 0.4:
            color = QColor(255, 165, 0)  # Orange
        else:
            color = QColor(244, 67, 54)  # Red
        
        painter.fillRect(x, y, confidence_width, bar_height, color)
        
        # Border
        painter.setPen(QPen(QColor(255, 255, 255), 1))
        painter.drawRect(x, y, bar_width, bar_height)
        
        # Text
        painter.setFont(QFont("Arial", 10, QFont.Weight.Bold))
        painter.setPen(QPen(QColor(255, 255, 255)))
        text = f"Confidence: {self.confidence_value * 100:.0f}%"
        painter.drawText(x + 5, y + 15, text)
    
    def mouseMoveEvent(self, event):
        """Handle mouse move events."""
        if self.calibration_mode:
            self.set_mouse_position(event.pos())
        super().mouseMoveEvent(event)
    
    def leaveEvent(self, event):
        """Handle mouse leave events."""
        self.mouse_pos = None
        self.update()
        super().leaveEvent(event)


class CalibrationHelper:
    """
    Helper class for managing calibration state and providing utilities.
    """
    
    @staticmethod
    def get_sample_region(x: int, y: int, size: int = 10) -> Tuple[int, int, int, int]:
        """
        Get the region around a sample point for color extraction.
        
        Args:
            x: X coordinate of the sample
            y: Y coordinate of the sample
            size: Size of the region (radius)
            
        Returns:
            Tuple of (x1, y1, x2, y2) defining the region
        """
        return (x - size, y - size, x + size, y + size)
    
    @staticmethod
    def validate_sample_position(x: int, y: int, width: int, height: int, 
                                 margin: int = 20) -> bool:
        """
        Validate that a sample position is within valid bounds.
        
        Args:
            x: X coordinate
            y: Y coordinate
            width: Image width
            height: Image height
            margin: Minimum margin from edges
            
        Returns:
            True if position is valid, False otherwise
        """
        return (margin <= x < width - margin and 
                margin <= y < height - margin)
    
    @staticmethod
    def calculate_sample_quality(hsv_values: List[Tuple[float, float, float]]) -> float:
        """
        Calculate quality score for a set of HSV samples.
        
        Args:
            hsv_values: List of (H, S, V) tuples
            
        Returns:
            Quality score from 0.0 to 1.0
        """
        if not hsv_values:
            return 0.0
        
        # Calculate variance in each channel
        import numpy as np
        hsv_array = np.array(hsv_values)
        
        # Lower variance = higher quality (more consistent color)
        h_var = np.var(hsv_array[:, 0])
        s_var = np.var(hsv_array[:, 1])
        v_var = np.var(hsv_array[:, 2])
        
        # Normalize and invert (lower variance = higher score)
        h_score = 1.0 - min(h_var / 180.0, 1.0)
        s_score = 1.0 - min(s_var / 255.0, 1.0)
        v_score = 1.0 - min(v_var / 255.0, 1.0)
        
        # Weighted average (hue is most important)
        quality = 0.5 * h_score + 0.3 * s_score + 0.2 * v_score
        
        return quality
    
    @staticmethod
    def get_lighting_recommendation(sample_count: int, 
                                   lighting_conditions: List[str]) -> str:
        """
        Get recommendation for next lighting condition to sample.
        
        Args:
            sample_count: Current number of samples
            lighting_conditions: List of lighting conditions already sampled
            
        Returns:
            Recommended lighting condition
        """
        recommended_order = ["bright", "dim", "mixed"]
        
        for condition in recommended_order:
            if condition not in [lc.lower() for lc in lighting_conditions]:
                return condition.capitalize()
        
        return "Custom"