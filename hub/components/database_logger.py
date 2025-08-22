"""
Database Logger Component

Handles logging of juggling data to SQLite database for analysis and replay.
"""

import sqlite3
import json
import time
import threading
from typing import Optional, Dict, Any
from datetime import datetime
import os
import sys

# Add the api directory to the path for protobuf imports
api_path = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(__file__))), 'api', 'v1')
sys.path.insert(0, api_path)

try:
    import juggler_pb2
except ImportError:
    print("❌ Error: Protocol Buffer files not found. Please run 'make generate-proto' first.")
    sys.exit(1)


class DatabaseLogger:
    """Database logger for juggling session data."""
    
    def __init__(self, db_path: str = "juggling_data.db"):
        self.db_path = db_path
        self.connection: Optional[sqlite3.Connection] = None
        self.lock = threading.Lock()
        
        # Session tracking
        self.current_session_id: Optional[str] = None
        self.session_start_time: Optional[float] = None
        
        # Statistics
        self.frames_logged = 0
        self.balls_logged = 0
        self.hands_logged = 0
        self.imu_data_logged = 0
        
        # Initialize database
        self._initialize_database()
    
    def _initialize_database(self):
        """Initialize the SQLite database with required tables."""
        try:
            self.connection = sqlite3.connect(self.db_path, check_same_thread=False)
            self.connection.execute("PRAGMA journal_mode=WAL")  # Better concurrency
            
            # Create tables
            self._create_tables()
            
            print(f"📊 Database initialized: {self.db_path}")
            
        except Exception as e:
            print(f"❌ Error initializing database: {e}")
            raise
    
    def _create_tables(self):
        """Create database tables."""
        cursor = self.connection.cursor()
        
        # Sessions table
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS sessions (
                session_id TEXT PRIMARY KEY,
                start_time REAL NOT NULL,
                end_time REAL,
                total_frames INTEGER DEFAULT 0,
                total_balls INTEGER DEFAULT 0,
                notes TEXT,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        """)
        
        # Frames table
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS frames (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                frame_number INTEGER NOT NULL,
                timestamp_us INTEGER NOT NULL,
                frame_width INTEGER,
                frame_height INTEGER,
                fps REAL,
                camera_fx REAL,
                camera_fy REAL,
                camera_ppx REAL,
                camera_ppy REAL,
                depth_scale REAL,
                system_status TEXT,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                FOREIGN KEY (session_id) REFERENCES sessions (session_id)
            )
        """)
        
        # Balls table
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS balls (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                frame_id INTEGER NOT NULL,
                ball_id TEXT NOT NULL,
                color_name TEXT NOT NULL,
                position_3d_x REAL NOT NULL,
                position_3d_y REAL NOT NULL,
                position_3d_z REAL NOT NULL,
                position_2d_x REAL NOT NULL,
                position_2d_y REAL NOT NULL,
                velocity_3d_x REAL,
                velocity_3d_y REAL,
                velocity_3d_z REAL,
                radius_px REAL,
                depth_m REAL,
                confidence REAL,
                is_held BOOLEAN,
                timestamp_us INTEGER NOT NULL,
                color_bgr_b INTEGER,
                color_bgr_g INTEGER,
                color_bgr_r INTEGER,
                FOREIGN KEY (session_id) REFERENCES sessions (session_id),
                FOREIGN KEY (frame_id) REFERENCES frames (id)
            )
        """)
        
        # Hands table
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS hands (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                frame_id INTEGER NOT NULL,
                side TEXT NOT NULL,
                position_2d_x REAL,
                position_2d_y REAL,
                position_3d_x REAL,
                position_3d_y REAL,
                position_3d_z REAL,
                confidence REAL,
                is_visible BOOLEAN,
                FOREIGN KEY (session_id) REFERENCES sessions (session_id),
                FOREIGN KEY (frame_id) REFERENCES frames (id)
            )
        """)
        
        # IMU data table
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS imu_data (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                frame_id INTEGER NOT NULL,
                watch_name TEXT NOT NULL,
                watch_ip TEXT,
                accel_x REAL NOT NULL,
                accel_y REAL NOT NULL,
                accel_z REAL NOT NULL,
                gyro_x REAL NOT NULL,
                gyro_y REAL NOT NULL,
                gyro_z REAL NOT NULL,
                mag_x REAL,
                mag_y REAL,
                mag_z REAL,
                accel_magnitude REAL,
                gyro_magnitude REAL,
                timestamp_us INTEGER NOT NULL,
                data_age_ms REAL,
                FOREIGN KEY (session_id) REFERENCES sessions (session_id),
                FOREIGN KEY (frame_id) REFERENCES frames (id)
            )
        """)
        
        # Create indexes for better query performance
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_frames_session_timestamp ON frames (session_id, timestamp_us)")
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_balls_session_frame ON balls (session_id, frame_id)")
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_balls_color_time ON balls (color_name, timestamp_us)")
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_hands_session_frame ON hands (session_id, frame_id)")
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_imu_session_frame ON imu_data (session_id, frame_id)")
        
        self.connection.commit()
    
    def start_session(self, session_id: Optional[str] = None, notes: str = "") -> str:
        """Start a new logging session."""
        with self.lock:
            if session_id is None:
                session_id = f"session_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
            
            self.current_session_id = session_id
            self.session_start_time = time.time()
            
            cursor = self.connection.cursor()
            cursor.execute("""
                INSERT INTO sessions (session_id, start_time, notes)
                VALUES (?, ?, ?)
            """, (session_id, self.session_start_time, notes))
            self.connection.commit()
            
            # Reset statistics
            self.frames_logged = 0
            self.balls_logged = 0
            self.hands_logged = 0
            self.imu_data_logged = 0
            
            print(f"📝 Started logging session: {session_id}")
            return session_id
    
    def end_session(self):
        """End the current logging session."""
        with self.lock:
            if self.current_session_id is None:
                return
            
            end_time = time.time()
            cursor = self.connection.cursor()
            cursor.execute("""
                UPDATE sessions 
                SET end_time = ?, total_frames = ?, total_balls = ?
                WHERE session_id = ?
            """, (end_time, self.frames_logged, self.balls_logged, self.current_session_id))
            self.connection.commit()
            
            duration = end_time - self.session_start_time if self.session_start_time else 0
            print(f"📝 Ended logging session: {self.current_session_id}")
            print(f"   Duration: {duration:.1f}s, Frames: {self.frames_logged}, Balls: {self.balls_logged}")
            
            self.current_session_id = None
            self.session_start_time = None
    
    def log_frame_data(self, frame_data: juggler_pb2.FrameData):
        """Log a complete frame of data."""
        if self.current_session_id is None:
            # Auto-start session if not already started
            self.start_session()
        
        with self.lock:
            try:
                cursor = self.connection.cursor()
                
                # Insert frame record
                cursor.execute("""
                    INSERT INTO frames (
                        session_id, frame_number, timestamp_us, frame_width, frame_height,
                        fps, camera_fx, camera_fy, camera_ppx, camera_ppy, depth_scale,
                        system_status
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """, (
                    self.current_session_id,
                    frame_data.frame_number,
                    frame_data.timestamp_us,
                    frame_data.frame_width,
                    frame_data.frame_height,
                    frame_data.status.fps,
                    frame_data.intrinsics.fx,
                    frame_data.intrinsics.fy,
                    frame_data.intrinsics.ppx,
                    frame_data.intrinsics.ppy,
                    frame_data.intrinsics.depth_scale,
                    json.dumps({
                        'camera_connected': frame_data.status.camera_connected,
                        'engine_running': frame_data.status.engine_running,
                        'mode': frame_data.status.mode,
                        'error_message': frame_data.status.error_message
                    })
                ))
                
                frame_id = cursor.lastrowid
                
                # Insert ball data
                for ball in frame_data.balls:
                    cursor.execute("""
                        INSERT INTO balls (
                            session_id, frame_id, ball_id, color_name,
                            position_3d_x, position_3d_y, position_3d_z,
                            position_2d_x, position_2d_y,
                            velocity_3d_x, velocity_3d_y, velocity_3d_z,
                            radius_px, depth_m, confidence, is_held, timestamp_us,
                            color_bgr_b, color_bgr_g, color_bgr_r
                        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """, (
                        self.current_session_id, frame_id, ball.id, ball.color_name,
                        ball.position_3d.x, ball.position_3d.y, ball.position_3d.z,
                        ball.position_2d.x, ball.position_2d.y,
                        ball.velocity_3d.x, ball.velocity_3d.y, ball.velocity_3d.z,
                        ball.radius_px, ball.depth_m, ball.confidence, ball.is_held,
                        ball.timestamp_us,
                        ball.color_bgr.b, ball.color_bgr.g, ball.color_bgr.r
                    ))
                    self.balls_logged += 1
                
                # Insert hand data
                for hand in frame_data.hands:
                    cursor.execute("""
                        INSERT INTO hands (
                            session_id, frame_id, side,
                            position_2d_x, position_2d_y,
                            position_3d_x, position_3d_y, position_3d_z,
                            confidence, is_visible
                        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """, (
                        self.current_session_id, frame_id, hand.side,
                        hand.position_2d.x, hand.position_2d.y,
                        hand.position_3d.x, hand.position_3d.y, hand.position_3d.z,
                        hand.confidence, hand.is_visible
                    ))
                    self.hands_logged += 1
                
                # Insert IMU data
                for imu in frame_data.imu_data:
                    cursor.execute("""
                        INSERT INTO imu_data (
                            session_id, frame_id, watch_name, watch_ip,
                            accel_x, accel_y, accel_z,
                            gyro_x, gyro_y, gyro_z,
                            mag_x, mag_y, mag_z,
                            accel_magnitude, gyro_magnitude,
                            timestamp_us, data_age_ms
                        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """, (
                        self.current_session_id, frame_id, imu.watch_name, imu.watch_ip,
                        imu.acceleration.x, imu.acceleration.y, imu.acceleration.z,
                        imu.gyroscope.x, imu.gyroscope.y, imu.gyroscope.z,
                        imu.magnetometer.x, imu.magnetometer.y, imu.magnetometer.z,
                        imu.accel_magnitude, imu.gyro_magnitude,
                        imu.timestamp_us, imu.data_age_ms
                    ))
                    self.imu_data_logged += 1
                
                self.connection.commit()
                self.frames_logged += 1
                
            except Exception as e:
                print(f"❌ Error logging frame data: {e}")
                self.connection.rollback()
    
    def get_statistics(self) -> Dict[str, Any]:
        """Get current logging statistics."""
        return {
            'current_session_id': self.current_session_id,
            'session_start_time': self.session_start_time,
            'frames_logged': self.frames_logged,
            'balls_logged': self.balls_logged,
            'hands_logged': self.hands_logged,
            'imu_data_logged': self.imu_data_logged,
            'db_path': self.db_path
        }
    
    def get_sessions(self) -> list:
        """Get list of all sessions."""
        with self.lock:
            cursor = self.connection.cursor()
            cursor.execute("""
                SELECT session_id, start_time, end_time, total_frames, total_balls, notes
                FROM sessions
                ORDER BY start_time DESC
            """)
            return cursor.fetchall()
    
    def cleanup(self):
        """Clean up database resources."""
        print("🧹 Cleaning up database logger...")
        
        # End current session if active
        if self.current_session_id:
            self.end_session()
        
        # Close database connection
        if self.connection:
            self.connection.close()
            self.connection = None
        
        print("✅ Database logger cleanup completed")
    
    def __del__(self):
        """Destructor to ensure cleanup."""
        self.cleanup()


# Example usage and testing
if __name__ == "__main__":
    # Create test logger
    logger = DatabaseLogger("test_juggling.db")
    
    # Create a test frame data
    frame_data = juggler_pb2.FrameData()
    frame_data.timestamp_us = int(time.time() * 1000000)
    frame_data.frame_number = 1
    frame_data.frame_width = 640
    frame_data.frame_height = 480
    
    # Add test ball
    ball = frame_data.balls.add()
    ball.id = "test_ball_1"
    ball.color_name = "red"
    ball.position_3d.x = 0.1
    ball.position_3d.y = 0.2
    ball.position_3d.z = 0.8
    ball.position_2d.x = 320
    ball.position_2d.y = 240
    ball.confidence = 0.95
    ball.timestamp_us = frame_data.timestamp_us
    
    # Set camera intrinsics
    frame_data.intrinsics.fx = 600.0
    frame_data.intrinsics.fy = 600.0
    frame_data.intrinsics.ppx = 320.0
    frame_data.intrinsics.ppy = 240.0
    
    # Set system status
    frame_data.status.camera_connected = True
    frame_data.status.engine_running = True
    frame_data.status.fps = 30.0
    frame_data.status.mode = "tracking"
    
    # Test logging
    print("🧪 Testing database logger...")
    session_id = logger.start_session("test_session", "Test session for database logger")
    
    # Log some test frames
    for i in range(5):
        frame_data.frame_number = i + 1
        frame_data.timestamp_us = int((time.time() + i * 0.033) * 1000000)  # 30 FPS
        ball.timestamp_us = frame_data.timestamp_us
        logger.log_frame_data(frame_data)
        time.sleep(0.1)
    
    logger.end_session()
    
    # Print statistics
    stats = logger.get_statistics()
    print(f"📊 Final stats: {stats}")
    
    # Print sessions
    sessions = logger.get_sessions()
    print(f"📝 Sessions: {sessions}")
    
    logger.cleanup()
    print("✅ Database logger test completed")