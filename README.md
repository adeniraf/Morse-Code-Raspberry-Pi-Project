# Morse-Code-Raspberry-Pi-Project


The purpose of this project is to use a mixture of C code and ARM assembly to build a simple game that will teach a player morse-code.

## Instructions
* The game should have a minimum of two levels.
* The first level for matching individual characters (with the expected Morse pattern provided) and the second for matching individual characters (without the expected Morse pattern provided).
* The player should interact with the game by pressing the GP21 button on the MAKER-PI-PICO board for a short duration to input a Morse “dot” and a longer duration to input a Morse “dash”. 
* If no new input is entered for at least 1 second after one-or-more “dots” or “dashes” have been input, then it should be considered a “space” character. 
* If no new input is entered for at least another 1 second after that, then the sequence should be considered complete and passed to the game portion of the code for pattern matching.  
