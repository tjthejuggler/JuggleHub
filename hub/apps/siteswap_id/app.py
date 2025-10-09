"""
Siteswap ID App

Identifies siteswap patterns by analyzing throw and catch sequences.
Tracks which hands throw which balls to which hands and determines the siteswap notation.
"""

from PyQt6.QtWidgets import (QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
                             QLabel, QPushButton, QTextEdit, QGroupBox, QScrollArea)
from PyQt6.QtCore import Qt, pyqtSignal, QObject
from PyQt6.QtGui import QFont, QColor
from apps.base import BaseApp
import juggler_pb2
from collections import deque, defaultdict
from typing import Dict, List, Tuple, Optional


class SiteswapSignal(QObject):
    """Signal emitter for thread-safe UI updates."""
    event_detected = pyqtSignal(object)  # ThrowCatchEvent
    raw_stream_updated = pyqtSignal(str)  # Raw siteswap stream
    clean_pattern_identified = pyqtSignal(str)  # Clean siteswap pattern
    stats_updated = pyqtSignal(dict)  # Statistics dictionary


class ThrowCatchSequence:
    """Tracks a sequence of throws and catches to identify patterns."""
    
    def __init__(self, max_history: int = 100):
        self.events: deque = deque(maxlen=max_history)
        self.throw_times: Dict[int, int] = {}  # ball_id -> timestamp of last throw
        self.catch_times: Dict[int, int] = {}  # ball_id -> timestamp of last catch
        self.hand_names = {0: "L", 1: "R"}
        
    def add_event(self, event):
        """Add a throw or catch event to the sequence."""
        self.events.append({
            'type': 'THROW' if event.type == juggler_pb2.ThrowCatchEvent.THROW else 'CATCH',
            'ball_id': event.ball_id,
            'hand_id': event.hand_id,
            'timestamp': event.timestamp_us,
            'confidence': event.confidence
        })
        
        if event.type == juggler_pb2.ThrowCatchEvent.THROW:
            self.throw_times[event.ball_id] = event.timestamp_us
        else:
            self.catch_times[event.ball_id] = event.timestamp_us
    
    def get_recent_pattern(self, num_throws: int = 10) -> List[Dict]:
        """Get the most recent throw events."""
        throws = [e for e in self.events if e['type'] == 'THROW']
        return list(throws)[-num_throws:] if len(throws) >= num_throws else []
    
    def calculate_dwell_time(self, ball_id: int) -> Optional[float]:
        """Calculate dwell time (catch to throw) for a ball in milliseconds."""
        if ball_id in self.catch_times and ball_id in self.throw_times:
            catch_time = self.catch_times[ball_id]
            throw_time = self.throw_times[ball_id]
            if throw_time > catch_time:
                return (throw_time - catch_time) / 1000.0  # Convert to ms
        return None
    
    def calculate_flight_time(self, ball_id: int) -> Optional[float]:
        """Calculate flight time (throw to catch) for a ball in milliseconds."""
        if ball_id in self.throw_times and ball_id in self.catch_times:
            throw_time = self.throw_times[ball_id]
            catch_time = self.catch_times[ball_id]
            if catch_time > throw_time:
                return (catch_time - throw_time) / 1000.0  # Convert to ms
        return None


class SiteswapAnalyzer:
    """Analyzes throw/catch sequences to identify siteswap patterns."""
    
    def __init__(self):
        self.sequence = ThrowCatchSequence()
        self.detected_patterns: List[str] = []
        
    def add_event(self, event):
        """Add an event and analyze for patterns."""
        self.sequence.add_event(event)
        
    def get_raw_stream(self, max_length: int = 30) -> str:
        """Get the raw stream of throw heights (most recent throws)."""
        all_throws = [e for e in self.sequence.events if e['type'] == 'THROW']
        
        if len(all_throws) < 1:
            return "..."
        
        # Get recent throws
        recent_throws = all_throws[-max_length:]
        heights = []
        
        for throw in recent_throws:
            ball_id = throw['ball_id']
            throw_time = throw['timestamp']
            
            # Find the next catch of this ball
            next_catch = None
            for event in self.sequence.events:
                if (event['type'] == 'CATCH' and
                    event['ball_id'] == ball_id and
                    event['timestamp'] > throw_time):
                    next_catch = event
                    break
            
            if next_catch:
                flight_time_ms = (next_catch['timestamp'] - throw_time) / 1000.0
                beat_time = 200.0
                height = round(flight_time_ms / beat_time)
                heights.append(str(max(0, min(9, height))))
            else:
                heights.append('?')  # Unknown if catch not found yet
        
        return ''.join(heights)
    
    def analyze_clean_pattern(self, num_throws: int = 20) -> Optional[str]:
        """
        Analyze for clean siteswap pattern by filtering out noise.
        Returns the most common repeating pattern.
        """
        all_throws = [e for e in self.sequence.events if e['type'] == 'THROW']
        
        if len(all_throws) < 6:
            return None
        
        # Use more throws for better pattern detection
        throws = all_throws[-num_throws:] if len(all_throws) >= num_throws else all_throws
        
        # Calculate throw heights
        heights = []
        for throw in throws:
            ball_id = throw['ball_id']
            throw_time = throw['timestamp']
            
            next_catch = None
            for event in self.sequence.events:
                if (event['type'] == 'CATCH' and
                    event['ball_id'] == ball_id and
                    event['timestamp'] > throw_time):
                    next_catch = event
                    break
            
            if next_catch:
                flight_time_ms = (next_catch['timestamp'] - throw_time) / 1000.0
                beat_time = 200.0
                height = round(flight_time_ms / beat_time)
                heights.append(max(0, min(9, height)))
            else:
                heights.append(3)  # Default
        
        if len(heights) < 6:
            return None
        
        # Find the most common repeating pattern
        pattern = self._find_dominant_pattern(heights)
        if pattern:
            return ''.join(map(str, pattern))
        
        return None
    
    def _find_dominant_pattern(self, sequence: List[int], min_length: int = 1, max_length: int = 5) -> Optional[List[int]]:
        """
        Find the dominant repeating pattern by filtering out noise.
        Uses frequency analysis to identify the core pattern.
        """
        seq_len = len(sequence)
        
        if seq_len < 6:
            return None
        
        # Try different pattern lengths
        best_pattern = None
        best_score = 0
        
        for pattern_len in range(min_length, min(max_length + 1, seq_len // 3 + 1)):
            # Count occurrences of each possible pattern of this length
            pattern_counts = defaultdict(int)
            
            for i in range(seq_len - pattern_len + 1):
                pattern_tuple = tuple(sequence[i:i + pattern_len])
                pattern_counts[pattern_tuple] += 1
            
            # Find the most common pattern
            if pattern_counts:
                most_common_pattern, count = max(pattern_counts.items(), key=lambda x: x[1])
                
                # Score based on frequency and coverage
                coverage = (count * pattern_len) / seq_len
                score = count * coverage
                
                if score > best_score and count >= 2:  # Must appear at least twice
                    best_score = score
                    best_pattern = list(most_common_pattern)
        
        return best_pattern
    
    def analyze_vanilla_siteswap(self, num_throws: int = 6) -> Optional[str]:
        """
        Analyze for vanilla (asynchronous) siteswap patterns.
        Returns siteswap notation if a repeating pattern is detected.
        """
        # Get all throws (not just recent ones)
        all_throws = [e for e in self.sequence.events if e['type'] == 'THROW']
        
        # Need at least 6 throws to detect a pattern
        if len(all_throws) < num_throws:
            return None
        
        # Use the most recent throws
        throws = all_throws[-num_throws:]
        
        # Calculate throw heights based on flight times
        heights = []
        for i, throw in enumerate(throws):
            ball_id = throw['ball_id']
            throw_time = throw['timestamp']
            
            # Find the next catch of this ball after this throw
            next_catch = None
            for event in self.sequence.events:
                if (event['type'] == 'CATCH' and
                    event['ball_id'] == ball_id and
                    event['timestamp'] > throw_time):
                    next_catch = event
                    break
            
            if next_catch:
                flight_time_ms = (next_catch['timestamp'] - throw_time) / 1000.0
                # Estimate throw height: each ~200ms of flight time ≈ 1 beat
                # Adjust beat_time based on average to be more adaptive
                beat_time = 200.0  # ms per beat (adjustable)
                height = round(flight_time_ms / beat_time)
                heights.append(max(0, min(9, height)))  # Clamp to 0-9
            else:
                # If no catch found yet, estimate based on typical patterns
                heights.append(3)  # Default to 3 (common for 3-ball cascade)
        
        # Check for repeating patterns
        pattern = self._find_repeating_pattern(heights)
        if pattern:
            return ''.join(map(str, pattern))
        
        # If no repeating pattern, show the raw sequence
        if len(heights) >= 3:
            return ''.join(map(str, heights[-3:]))  # Show last 3 throws
        
        return None
    
    def analyze_synchronous_siteswap(self) -> Optional[str]:
        """
        Analyze for synchronous siteswap patterns (both hands throw simultaneously).
        Returns siteswap notation like (4,4) or (6x,4) if detected.
        """
        # Group events by approximate timestamp (within 100ms window)
        time_window = 100000  # 100ms in microseconds
        
        throws = [e for e in self.sequence.events if e['type'] == 'THROW']
        if len(throws) < 4:
            return None
        
        # Find simultaneous throws
        sync_pairs = []
        i = 0
        while i < len(throws) - 1:
            left_throw = throws[i]
            right_throw = throws[i + 1]
            
            if abs(left_throw['timestamp'] - right_throw['timestamp']) < time_window:
                if left_throw['hand_id'] != right_throw['hand_id']:
                    sync_pairs.append((left_throw, right_throw))
                    i += 2
                else:
                    i += 1
            else:
                i += 1
        
        if len(sync_pairs) < 2:
            return None
        
        # Analyze the pairs for pattern
        # This is a simplified version - full implementation would track crossing throws
        return None  # Placeholder for now
    
    def _find_repeating_pattern(self, sequence: List[int], min_length: int = 1, max_length: int = 5) -> Optional[List[int]]:
        """Find the shortest repeating pattern in a sequence."""
        seq_len = len(sequence)
        
        # Try to find patterns from shortest to longest
        for pattern_len in range(min_length, min(max_length + 1, seq_len // 2 + 1)):
            pattern = sequence[:pattern_len]
            is_repeating = True
            repetitions = 0
            
            # Check if pattern repeats at least once
            for i in range(pattern_len, seq_len):
                if sequence[i] != pattern[i % pattern_len]:
                    is_repeating = False
                    break
                if (i + 1) % pattern_len == 0:
                    repetitions += 1
            
            # Accept pattern if it repeats at least once
            if is_repeating and repetitions >= 1:
                return pattern
        
        return None
    
    def get_statistics(self) -> Dict:
        """Get statistics about the current juggling session."""
        events = list(self.sequence.events)
        throws = [e for e in events if e['type'] == 'THROW']
        catches = [e for e in events if e['type'] == 'CATCH']
        
        # Count by hand
        left_throws = len([t for t in throws if t['hand_id'] == 0])
        right_throws = len([t for t in throws if t['hand_id'] == 1])
        left_catches = len([c for c in catches if c['hand_id'] == 0])
        right_catches = len([c for c in catches if c['hand_id'] == 1])
        
        # Calculate average flight times
        flight_times = []
        for ball_id in set(e['ball_id'] for e in events):
            ft = self.sequence.calculate_flight_time(ball_id)
            if ft:
                flight_times.append(ft)
        
        avg_flight_time = sum(flight_times) / len(flight_times) if flight_times else 0
        
        # Calculate average dwell times
        dwell_times = []
        for ball_id in set(e['ball_id'] for e in events):
            dt = self.sequence.calculate_dwell_time(ball_id)
            if dt:
                dwell_times.append(dt)
        
        avg_dwell_time = sum(dwell_times) / len(dwell_times) if dwell_times else 0
        
        return {
            'total_throws': len(throws),
            'total_catches': len(catches),
            'left_throws': left_throws,
            'right_throws': right_throws,
            'left_catches': left_catches,
            'right_catches': right_catches,
            'avg_flight_time_ms': round(avg_flight_time, 1),
            'avg_dwell_time_ms': round(avg_dwell_time, 1),
            'num_balls': len(set(e['ball_id'] for e in events))
        }


class SiteswapIDApp(BaseApp):
    """Siteswap pattern identification application."""
    
    def get_metadata(self) -> dict:
        """Return app metadata."""
        return {
            "id": "siteswap_id",
            "name": "Siteswap ID",
            "version": "1.0.0"
        }
    
    def initialize(self):
        """Initialize the app."""
        self.analyzer = SiteswapAnalyzer()
        self.signal_emitter = SiteswapSignal()
        self.signal_emitter.event_detected.connect(self._on_event)
        self.signal_emitter.raw_stream_updated.connect(self._on_raw_stream)
        self.signal_emitter.clean_pattern_identified.connect(self._on_clean_pattern)
        self.signal_emitter.stats_updated.connect(self._on_stats)
        
        self.raw_stream = "..."
        self.clean_pattern = "Detecting..."
        self.event_log = []
        self.ball_colors: Dict[int, str] = {}  # ball_id -> color_name
        
        # Color mapping for display (RGB hex colors)
        self.color_map = {
            'red': '#FF4444',
            'orange': '#FF8844',
            'yellow': '#FFDD44',
            'green': '#44FF44',
            'blue': '#4444FF',
            'purple': '#AA44FF',
            'pink': '#FF44AA',
            'white': '#FFFFFF',
        }
        
        # Enable throw/catch detection feature
        self.api.enable_feature("throw_catch_detection")
    
    def create_window(self) -> QMainWindow:
        """Create and return the app window."""
        window = QMainWindow()
        window.setWindowTitle("Siteswap ID")
        window.setGeometry(100, 100, 800, 600)
        
        # Central widget with scroll area
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        window.setCentralWidget(scroll)
        
        central = QWidget()
        scroll.setWidget(central)
        layout = QVBoxLayout(central)
        layout.setSpacing(15)
        layout.setContentsMargins(20, 20, 20, 20)
        
        # Title
        title = QLabel("🔢 Siteswap ID")
        title.setFont(QFont("Arial", 24, QFont.Weight.Bold))
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)
        
        # Raw Stream Display
        stream_group = QGroupBox("Raw Siteswap Stream")
        stream_layout = QVBoxLayout()
        
        self.stream_label = QLabel(self.raw_stream)
        self.stream_label.setFont(QFont("Courier New", 18))
        self.stream_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.stream_label.setStyleSheet("color: #FFA500;")
        self.stream_label.setWordWrap(True)
        stream_layout.addWidget(self.stream_label)
        
        stream_group.setLayout(stream_layout)
        layout.addWidget(stream_group)
        
        # Clean Pattern Display
        pattern_group = QGroupBox("Clean Pattern (Noise Filtered)")
        pattern_layout = QVBoxLayout()
        
        self.pattern_label = QLabel(self.clean_pattern)
        self.pattern_label.setFont(QFont("Courier New", 48, QFont.Weight.Bold))
        self.pattern_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.pattern_label.setStyleSheet("color: #4CAF50;")
        pattern_layout.addWidget(self.pattern_label)
        
        pattern_group.setLayout(pattern_layout)
        layout.addWidget(pattern_group)
        
        # Statistics Display
        stats_group = QGroupBox("Statistics")
        stats_layout = QVBoxLayout()
        
        self.stats_label = QLabel(self._format_stats({}))
        self.stats_label.setFont(QFont("Courier New", 11))
        self.stats_label.setAlignment(Qt.AlignmentFlag.AlignLeft)
        stats_layout.addWidget(self.stats_label)
        
        stats_group.setLayout(stats_layout)
        layout.addWidget(stats_group)
        
        # Event Log
        log_group = QGroupBox("Event Log (Recent 20)")
        log_layout = QVBoxLayout()
        
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setFont(QFont("Courier New", 10))
        self.log_text.setMaximumHeight(200)
        self.log_text.setAcceptRichText(True)  # Enable HTML formatting
        log_layout.addWidget(self.log_text)
        
        log_group.setLayout(log_layout)
        layout.addWidget(log_group)
        
        # Control Buttons
        button_layout = QHBoxLayout()
        
        reset_btn = QPushButton("Reset Analysis")
        reset_btn.setFixedHeight(40)
        reset_btn.setFont(QFont("Arial", 12, QFont.Weight.Bold))
        reset_btn.clicked.connect(self._reset_analysis)
        reset_btn.setStyleSheet("""
            QPushButton {
                background-color: #555555;
                color: white;
                border: none;
                padding: 10px;
                border-radius: 5px;
            }
            QPushButton:hover {
                background-color: #666666;
            }
            QPushButton:pressed {
                background-color: #444444;
            }
        """)
        button_layout.addWidget(reset_btn)
        
        layout.addLayout(button_layout)
        layout.addStretch()
        
        # Apply dark theme
        window.setStyleSheet("""
            QMainWindow, QWidget, QScrollArea {
                background-color: #2b2b2b;
                color: #ffffff;
            }
            QGroupBox {
                border: 2px solid #555555;
                border-radius: 5px;
                margin-top: 10px;
                padding-top: 10px;
                font-weight: bold;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 5px;
            }
            QTextEdit {
                background-color: #1e1e1e;
                border: 1px solid #555555;
                border-radius: 3px;
                padding: 5px;
            }
        """)
        
        return window
    
    def on_frame_data(self, frame_data):
        """
        Process incoming frame data from the engine.
        
        Args:
            frame_data: FrameData protobuf message
        """
        # Update ball color mapping from color_tracked_balls
        if hasattr(frame_data, 'color_tracked_balls'):
            for ball in frame_data.color_tracked_balls:
                if ball.is_active and ball.color_name:
                    self.ball_colors[ball.logical_id] = ball.color_name
        
        # Check for throw/catch events
        if hasattr(frame_data, 'throw_catch_events'):
            for event in frame_data.throw_catch_events:
                self.signal_emitter.event_detected.emit(event)
    
    def _on_event(self, event):
        """Handle a throw or catch event (thread-safe)."""
        # Add to analyzer
        self.analyzer.add_event(event)
        
        # Update event log with color coding
        event_type = "THROW" if event.type == juggler_pb2.ThrowCatchEvent.THROW else "CATCH"
        hand = "L" if event.hand_id == 0 else "R"
        timestamp_ms = event.timestamp_us / 1000.0
        
        # Get ball color
        ball_color_name = self.ball_colors.get(event.ball_id, 'white')
        base_color = self.color_map.get(ball_color_name, '#FFFFFF')
        
        # Make throws brighter, catches darker
        if event_type == "THROW":
            # Brighten the color for throws
            color = self._brighten_color(base_color, 1.3)
        else:
            # Darken the color for catches
            color = self._darken_color(base_color, 0.7)
        
        # Create HTML formatted log entry
        log_entry = f'<span style="color: {color};">[{timestamp_ms:10.1f}ms] {event_type:5s} | Ball {event.ball_id} | Hand {hand} | Conf: {event.confidence:.2f}</span>'
        self.event_log.append(log_entry)
        
        # Keep only last 20 events
        if len(self.event_log) > 20:
            self.event_log.pop(0)
        
        # Update log with HTML formatting
        html_log = '<br>'.join(reversed(self.event_log))
        self.log_text.setHtml(html_log)
        
        # Update raw stream and clean pattern (only on throw events)
        if event_type == "THROW":
            # Update raw stream
            raw_stream = self.analyzer.get_raw_stream(max_length=30)
            if raw_stream != self.raw_stream:
                self.signal_emitter.raw_stream_updated.emit(raw_stream)
            
            # Update clean pattern
            clean_pattern = self.analyzer.analyze_clean_pattern(num_throws=20)
            if clean_pattern and clean_pattern != self.clean_pattern:
                self.signal_emitter.clean_pattern_identified.emit(clean_pattern)
        
        # Update statistics
        stats = self.analyzer.get_statistics()
        self.signal_emitter.stats_updated.emit(stats)
    
    def _on_raw_stream(self, stream: str):
        """Update the raw stream display (thread-safe)."""
        self.raw_stream = stream
        self.stream_label.setText(stream)
    
    def _on_clean_pattern(self, pattern: str):
        """Update the clean pattern display (thread-safe)."""
        self.clean_pattern = pattern
        self.pattern_label.setText(pattern)
    
    def _on_stats(self, stats: Dict):
        """Update the statistics display (thread-safe)."""
        self.stats_label.setText(self._format_stats(stats))
    
    def _format_stats(self, stats: Dict) -> str:
        """Format statistics for display."""
        if not stats:
            return "Waiting for data..."
        
        return f"""Total Throws:      {stats.get('total_throws', 0)}
Total Catches:     {stats.get('total_catches', 0)}

Left Hand:         {stats.get('left_throws', 0)} throws, {stats.get('left_catches', 0)} catches
Right Hand:        {stats.get('right_throws', 0)} throws, {stats.get('right_catches', 0)} catches

Avg Flight Time:   {stats.get('avg_flight_time_ms', 0)} ms
Avg Dwell Time:    {stats.get('avg_dwell_time_ms', 0)} ms

Number of Balls:   {stats.get('num_balls', 0)}"""
    
    def _brighten_color(self, hex_color: str, factor: float) -> str:
        """Brighten a hex color by a factor."""
        # Remove '#' if present
        hex_color = hex_color.lstrip('#')
        
        # Convert to RGB
        r = int(hex_color[0:2], 16)
        g = int(hex_color[2:4], 16)
        b = int(hex_color[4:6], 16)
        
        # Brighten (move towards 255)
        r = min(255, int(r + (255 - r) * (factor - 1)))
        g = min(255, int(g + (255 - g) * (factor - 1)))
        b = min(255, int(b + (255 - b) * (factor - 1)))
        
        return f'#{r:02x}{g:02x}{b:02x}'
    
    def _darken_color(self, hex_color: str, factor: float) -> str:
        """Darken a hex color by a factor."""
        # Remove '#' if present
        hex_color = hex_color.lstrip('#')
        
        # Convert to RGB
        r = int(hex_color[0:2], 16)
        g = int(hex_color[2:4], 16)
        b = int(hex_color[4:6], 16)
        
        # Darken (multiply by factor)
        r = max(0, int(r * factor))
        g = max(0, int(g * factor))
        b = max(0, int(b * factor))
        
        return f'#{r:02x}{g:02x}{b:02x}'
    
    def _reset_analysis(self):
        """Reset the analysis and clear all data."""
        self.analyzer = SiteswapAnalyzer()
        self.raw_stream = "..."
        self.clean_pattern = "Detecting..."
        self.event_log = []
        self.ball_colors = {}
        self.stream_label.setText(self.raw_stream)
        self.pattern_label.setText(self.clean_pattern)
        self.log_text.clear()
        self.stats_label.setText(self._format_stats({}))
    
    def cleanup(self):
        """Clean up resources."""
        # Disable throw/catch detection feature
        self.api.disable_feature("throw_catch_detection")