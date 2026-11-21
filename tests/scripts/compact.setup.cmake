# Setup of the script case "compact" (P31, T-REC-36), included by
# run_script.cmake: an audio track whose take is a WAV file in the project.
file(MAKE_DIRECTORY "${WORK}/project/takes/take-20261121-130000")
file(COPY_FILE "${setupDir}/compact-vocal.wav" "${WORK}/project/takes/take-20261121-130000/01-Vocal.wav")
file(WRITE "${WORK}/project/project.json"
"{\"format\": \"mini-daw project\", \"version\": 1, \"sampleRate\": 48000, \"tracks\": [
  {\"number\": 1, \"name\": \"Vocal\", \"kind\": \"audio\", \"inputChannels\": [0],
   \"armed\": false, \"monitor\": false, \"volumeDb\": 0, \"mute\": false, \"solo\": false,
   \"take\": \"takes/take-20261121-130000/01-Vocal.wav\", \"takeStart\": 0}]}
")
