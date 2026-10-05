# System Context

The vehicle consists of a real-time flight-control domain and a companion perception domain. The flight controller owns stabilization, actuator authority, health supervision, and safety enforcement. The companion computer provides perception and bounded navigation/avoidance requests.

The design must remain safe when the companion computer is unavailable.
