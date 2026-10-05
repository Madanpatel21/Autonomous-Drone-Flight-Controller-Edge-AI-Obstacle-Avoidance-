# Prompt Execution Index

## Start
Read `START_HERE.md`, then `00_AGENT_CONTRACT.md`.

## Execution order

1. 01_01_discovery.prompt.md
2. 02_02_requirements.prompt.md
3. 03_03_architecture.prompt.md
4. 04_04_components.prompt.md
5. 05_05_power.prompt.md
6. 06_06_schematic.prompt.md
7. 07_07_pcb.prompt.md
8. 08_08_bringup.prompt.md
9. 09_09_firmware.prompt.md
10. 10_10_drivers.prompt.md
11. 11_11_estimation.prompt.md
12. 12_12_control.prompt.md
13. 13_13_motor_esc.prompt.md
14. 14_14_navigation.prompt.md
15. 15_15_ai_data.prompt.md
16. 16_16_ai_inference.prompt.md
17. 17_17_perception.prompt.md
18. 18_18_avoidance.prompt.md
19. 19_19_communication.prompt.md
20. 20_20_ground_station.prompt.md
21. 21_21_simulation.prompt.md
22. 22_22_hil.prompt.md
23. 23_23_verification.prompt.md
24. 24_24_safety.prompt.md
25. 25_25_manufacturing.prompt.md
26. 26_26_documentation.prompt.md
27. 27_27_vnv.prompt.md
28. 28_28_release.prompt.md
29. 29_29_final_audit.prompt.md

## Cross-phase rule
`30_target_strategy.prompt.md` is always applicable when a phase touches hardware/software boundaries. The agent must not wait for physical STM32 availability.

## Chaining rule
After each phase, update status/evidence, return here, find the first incomplete phase, and execute it. Never ask the user for the next prompt.
