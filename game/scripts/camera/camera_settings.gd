class_name CameraSettings
extends Resource
## Every tunable of the city camera, so feel can be adjusted from one `.tres` without touching code.
## Distances are meters, angles degrees, rates are "per second" (see `CityCamera` for how they are used).

@export_group("Pan")
## Pan speed as a fraction of the zoom distance per second, so panning feels the same near and far.
@export var pan_speed_per_distance: float = 1.1
## Multiplier while the cam_fast action (Shift) is held.
@export var fast_multiplier: float = 3.0
@export var edge_pan_enabled: bool = true
## Width of the screen-edge band that pans the camera, in pixels.
@export_range(0, 100) var edge_pan_margin_px: int = 8

@export_group("Rotation")
@export var rotate_speed_deg: float = 90.0
## Degrees of yaw per pixel of mouse movement while cam_rotate_drag is held.
@export var drag_rotate_deg_per_px: float = 0.25
@export var invert_drag_rotation: bool = false

@export_group("Zoom")
@export var zoom_min: float = 15.0
@export var zoom_max: float = 900.0
## Each wheel notch multiplies the zoom distance by this (or its inverse), so zoom is faster when far away.
@export_range(0.5, 0.99) var zoom_step_factor: float = 0.85
@export var invert_zoom: bool = false
## Camera pitch in degrees over normalized zoom (0 = closest, 1 = farthest). Null uses 25 near, 70 far.
@export var pitch_curve: Curve

@export_group("Smoothing")
## Exponential smoothing rates (1/s): higher is snappier. 0 turns smoothing off.
@export var position_smoothing: float = 12.0
@export var rotation_smoothing: float = 14.0
@export var zoom_smoothing: float = 10.0

@export_group("Bounds")
## The pivot stays inside this rectangle (x, z in meters). The test map is 2 km x 2 km from the origin.
@export var bounds: Rect2 = Rect2(0.0, 0.0, 2000.0, 2000.0)
