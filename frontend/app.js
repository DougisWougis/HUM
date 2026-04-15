document.addEventListener('DOMContentLoaded', () => {
    const humBtn = document.getElementById('humBtn');
    const recordTimer = document.getElementById('recordTimer');
    const fileInput = document.getElementById('audioFile');
    const fileNameDisplay = document.getElementById('fileNameDisplay');
    const inputAudioPlayer = document.getElementById('inputAudioPlayer');
    
    const loadingIndicator = document.getElementById('loadingIndicator');
    const resultsDashboard = document.getElementById('resultsDashboard');
    const resultsContent = document.getElementById('resultsContent');
    const flowchartContainer = document.getElementById('flowchartContainer');
    
    let mediaRecorder;
    let audioChunks = [];
    let recordedAudioBlob = null;
    let recordingExtension = 'webm';
    let isRecording = false;
    let startTime;
    let timerInterval;

    // --- 1. WEB AUDIO API METRONOME ---
    // Initialize the audio context for our count-in beeps
    const audioCtx = new (window.AudioContext || window.webkitAudioContext)();

    function playBeep(frequency) {
        // Resume context if browser suspended it
        if (audioCtx.state === 'suspended') audioCtx.resume();
        
        const oscillator = audioCtx.createOscillator();
        const gainNode = audioCtx.createGain();
        
        oscillator.type = 'sine';
        oscillator.frequency.setValueAtTime(frequency, audioCtx.currentTime);
        
        // Quick volume envelope to make it a short "click"
        gainNode.gain.setValueAtTime(0.1, audioCtx.currentTime);
        gainNode.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + 0.1);
        
        oscillator.connect(gainNode);
        gainNode.connect(audioCtx.destination);
        
        oscillator.start();
        oscillator.stop(audioCtx.currentTime + 0.1);
    }

    // --- 2. FILE UPLOAD LISTENER ---
    fileInput.addEventListener('change', () => {
        if (fileInput.files.length > 0) {
            recordedAudioBlob = null;
            fileNameDisplay.textContent = fileInput.files[0].name;
            humBtn.textContent = 'PROCESS';
            humBtn.classList.remove('recording');
            recordTimer.classList.add('hidden');
            
            inputAudioPlayer.src = URL.createObjectURL(fileInput.files[0]);
            inputAudioPlayer.classList.remove('hidden');
        } else {
            fileNameDisplay.textContent = '';
            humBtn.textContent = 'HUM';
            inputAudioPlayer.classList.add('hidden');
            inputAudioPlayer.src = '';
        }
    });

    // --- 3. THE MAIN ACTION BUTTON ---
    humBtn.addEventListener('click', async () => {
        
        // SCENARIO A: Re-processing an existing recording/upload
        if ((recordedAudioBlob || fileInput.files.length > 0) && !isRecording && humBtn.textContent === 'PROCESS') {
            await sendToEngine();
            return;
        }

        // SCENARIO B: User wants to STOP recording and process it
        if (isRecording) {
            mediaRecorder.stop();
            isRecording = false;
            
            clearInterval(timerInterval);
            humBtn.classList.remove('recording');
            humBtn.textContent = 'PROCESSING...';
            return; 
        }

        // SCENARIO C: User wants to START live recording (WITH COUNT-IN)
        try {
            const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
            mediaRecorder = new MediaRecorder(stream);
            audioChunks = [];

            // Reset UI
            fileInput.value = '';
            fileNameDisplay.textContent = '';
            resultsDashboard.classList.add('hidden');
            inputAudioPlayer.classList.add('hidden');
            inputAudioPlayer.src = '';

            mediaRecorder.ondataavailable = (event) => {
                if (event.data.size > 0) audioChunks.push(event.data);
            };

            mediaRecorder.onstop = async () => {
                const mimeType = mediaRecorder.mimeType || 'audio/webm';
                recordingExtension = mimeType.includes('mp4') ? 'mp4' : (mimeType.includes('ogg') ? 'ogg' : 'webm');
                recordedAudioBlob = new Blob(audioChunks, { type: mimeType });
                
                stream.getTracks().forEach(track => track.stop());
                
                inputAudioPlayer.src = URL.createObjectURL(recordedAudioBlob);
                inputAudioPlayer.classList.remove('hidden');

                await sendToEngine();
            };

            // --- THE COUNTDOWN SEQUENCE ---
            humBtn.disabled = true; // Prevent clicking during countdown
            humBtn.classList.add('recording');
            
            let count = 4;
            humBtn.textContent = count;
            playBeep(880); // High beep for beat 4

            const countInterval = setInterval(() => {
                count--;
                if (count > 0) {
                    humBtn.textContent = count;
                    playBeep(440); // Lower beeps for 3, 2, 1
                } else {
                    // Countdown finished! Start actual recording.
                    clearInterval(countInterval);
                    humBtn.disabled = false;
                    humBtn.textContent = 'STOP';
                    
                    mediaRecorder.start();
                    isRecording = true;
                    
                    recordTimer.classList.remove('hidden');
                    startTime = Date.now();
                    timerInterval = setInterval(() => {
                        const elapsed = Math.floor((Date.now() - startTime) / 1000);
                        recordTimer.textContent = `00:${String(elapsed).padStart(2, '0')}`;
                    }, 1000);
                }
            }, 1000); // 1000ms = 60 BPM tempo

        } catch (err) {
            console.error("Microphone access denied:", err);
            alert("Please allow microphone access to use the HUM feature.");
        }
    });

    // --- 4. THE ENGINE PIPELINE ---
    // --- 4. THE ENGINE PIPELINE (HTTP STREAMING) ---
    async function sendToEngine() {
        const formData = new FormData();
        formData.append('instrument', document.getElementById('instrument').value);
        formData.append('backingInstrument', document.getElementById('backingInstrument').value);
        formData.append('zcr', document.getElementById('zcr').checked);
        formData.append('flux', document.getElementById('flux').checked);
        formData.append('delta', document.getElementById('delta').checked);

        if (recordedAudioBlob) {
            formData.append('audioFile', recordedAudioBlob, `live_hum.${recordingExtension}`);
        } else {
            formData.append('audioFile', fileInput.files[0]);
        }

        // --- UI Setup ---
        humBtn.disabled = true;
        humBtn.textContent = 'PROCESSING...';
        resultsContent.classList.add('hidden');
        flowchartContainer.classList.remove('hidden');

        // Reset the flowchart visuals
        for (let i = 1; i <= 6; i++) {
            document.getElementById(`step${i}`).className = 'flowchart-step';
        }

        try {
            const response = await fetch('/api/process', { method: 'POST', body: formData });
            
            // --- THE STREAM READER ---
            const reader = response.body.getReader();
            const decoder = new TextDecoder('utf-8');
            let buffer = '';

            // Read the data chunks as they arrive from Node.js
            while (true) {
                const { done, value } = await reader.read();
                if (done) break; // The stream is finished

                // Decode the chunk from binary to text and add it to our buffer
                buffer += decoder.decode(value, { stream: true });
                
                // Split by newline to get individual JSON objects
                const lines = buffer.split('\n');
                
                // Keep the last line in the buffer if it's incomplete
                buffer = lines.pop();

                // Process each complete JSON line
                for (const line of lines) {
                    if (!line.trim()) continue; // Skip empty lines
                    
                    const data = JSON.parse(line);

                    // If it's a progress update, sync the flowchart trail!
                    if (data.type === 'progress') {
                        const currentStepNum = data.step;
                        
                        for (let i = 1; i <= 6; i++) {
                            const stepEl = document.getElementById(`step${i}`);
                            if (i < currentStepNum) {
                                // Previous steps become Orange
                                stepEl.className = 'flowchart-step visited'; 
                            } else if (i === currentStepNum) {
                                // The current step becomes Blue
                                stepEl.className = 'flowchart-step active'; 
                            } else {
                                // Future steps stay gray
                                stepEl.className = 'flowchart-step'; 
                            }
                        }
                    }
                    // If it's the final payload, populate the dashboard
                    else if (data.type === 'complete') {
                        if (data.success) {
                            document.getElementById('outputAudio').src = data.audioUrl;
                            document.getElementById('keyDisplay').textContent = data.analytics.key;
                            document.getElementById('bpmDisplay').textContent = data.analytics.bpm;
                            document.getElementById('chordDisplay').textContent = data.analytics.chords;
                            
                            humBtn.textContent = 'PROCESS';
                            recordTimer.classList.add('hidden');
                        }
                    } 
                    // If it's an error catch
                    else if (data.type === 'error') {
                        throw new Error(data.error);
                    }
                }
            }
        } catch (error) {
            console.error(error);
            alert("Engine Error: " + error.message);
            humBtn.textContent = recordedAudioBlob || fileInput.files.length > 0 ? 'PROCESS' : 'HUM';
        } finally {
            // --- UI Teardown ---
            humBtn.disabled = false;
            flowchartContainer.classList.add('hidden');
            resultsContent.classList.remove('hidden');
        }
    }
});