# Setup of the script case "exportaudio" (P29, T-PRJ-12), included by
# run_script.cmake: the MIDI track gets a take (two notes), so the song has
# something for the test synth to play.
file(MAKE_DIRECTORY "${WORK}/project/takes/take-20261121-120000")
file(COPY_FILE "${setupDir}/exportaudio-keys.mid" "${WORK}/project/takes/take-20261121-120000/01-Keys.mid")
file(WRITE "${WORK}/project/project.json"
"{\"format\": \"mini-daw project\", \"version\": 1, \"sampleRate\": 48000, \"tracks\": [
  {\"number\": 1, \"name\": \"Keys\", \"kind\": \"midi\", \"midiInput\": \"\", \"midiChannel\": 0,
   \"midiOutput\": \"\", \"armed\": false, \"volumeDb\": 0, \"mute\": false, \"solo\": false,
   \"take\": \"takes/take-20261121-120000/01-Keys.mid\", \"takeStart\": 0}]}
")
