const { spawn } = require('child_process');
const express = require('express');
const multer = require('multer');
const ffmpeg = require('fluent-ffmpeg');
const ffmpegPath = require('ffmpeg-static');
const path = require('path');
const fs = require('fs');
const cors = require('cors');

const app = express();

ffmpeg.setFfmpegPath(ffmpegPath);
app.use(cors());

// Best Practice: Define absolute paths at the top so the app never gets lost
const UPLOADS_DIR = path.join(__dirname, 'uploads');
const OUTPUTS_DIR = path.join(__dirname, 'outputs');
const FRONTEND_DIR = path.join(__dirname, '../frontend');

app.use('/outputs', express.static(OUTPUTS_DIR));
app.use(express.static(FRONTEND_DIR));

// Boot sequence: Use absolute paths
if (!fs.existsSync(UPLOADS_DIR)) fs.mkdirSync(UPLOADS_DIR);
if (!fs.existsSync(OUTPUTS_DIR)) fs.mkdirSync(OUTPUTS_DIR);

// --- STAGE 1: MULTER INTAKE CONFIGURATION ---
const storage = multer.diskStorage({
    destination: (req, file, cb) => {
        cb(null, UPLOADS_DIR); 
    },
    filename: (req, file, cb) => {
        const uniqueSuffix = Date.now() + '-' + Math.round(Math.random() * 1E9);
        cb(null, uniqueSuffix + path.extname(file.originalname));
    }
});

const upload = multer({ storage: storage });

// --- THE MAIN API ENDPOINT ---
app.post('/api/process', upload.single('audioFile'), (req, res) => {
    if (!req.file) {
        return res.status(400).json({ error: "No audio file uploaded." });
    }

    console.log(`\n[1/6 Intake] Received file: ${req.file.filename}`);

    // Security: Whitelist allowed instruments to prevent bad input
    const allowedInstruments = ['piano','harpsichord','organ','saxophone','acoustic guitar','synth lead','trumpet','halo pad','choir','violin', 'cello','flute','pad'];
    const instrument = allowedInstruments.includes(req.body.instrument) ? req.body.instrument : 'piano';

    // Catch the Backing Instrument
    const allowedBacking = ['electric piano', 'pad', 'strings'];
    const backingInstrument = allowedBacking.includes(req.body.backingInstrument) ? req.body.backingInstrument : 'jazz';
    
    const useZcr = req.body.zcr === 'true';
    const useFlux = req.body.flux === 'true';
    const useDelta = req.body.delta === 'true';

    // Internal disk paths (using path.join for OS safety)
    const uploadedFilePath = req.file.path;
    const standardWavPath = path.join(UPLOADS_DIR, `temp_${req.file.filename}.wav`);
    const finalOutputPath = path.join(OUTPUTS_DIR, `final_${req.file.filename}.wav`);
    
    // Web URL path (Forcing forward slashes for the browser)
    const webAudioUrl = `http://localhost:3000/outputs/final_${req.file.filename}.wav`;

    console.log("[2/6 Standardization] Starting FFmpeg conversion...");

    // --- STAGE 2: FFMPEG STANDARDIZATION ---
    ffmpeg(uploadedFilePath)
        .toFormat('wav')
        .audioFrequency(44100)
        .audioChannels(1)
        .on('end', () => {
            console.log("[2/6 Standardization] Complete. Audio is normalized.");
            console.log("\n[3/6 Execution] Waking up C++ HUM Engine...");

            // --- HTTP STREAMING SETUP ---
            // Tell the browser: "Keep the connection open, I'm going to send data in chunks."
            res.setHeader('Content-Type', 'application/x-ndjson');
            res.setHeader('Transfer-Encoding', 'chunked');
            res.flushHeaders(); 

            const args = [];
            if (useZcr) args.push('--zcr');
            if (useFlux) args.push('--flux');
            if (useDelta) args.push('--delta');
            args.push(standardWavPath, instrument, backingInstrument, finalOutputPath);

            // --- THE WINDOWS PATH FIX ---
            // 1. Define the exact directory where the .exe AND the .onnx model live
            const engineDirectory = path.join(__dirname, '../engine/build/Debug');

            // 2. Spawn the executable, forcing its working directory to the Debug folder
            const humEngine = spawn(path.join(engineDirectory, 'HUM_Engine.exe'), args, { 
                cwd: engineDirectory 
            });
            let engineTextOutput = "";

            humEngine.stdout.on('data', (data) => {
                const text = data.toString();
                process.stdout.write(`[C++] ${text}`); 
                engineTextOutput += text;

                // --- THE REAL-TIME SYNC ---
                // Look for strings like "[1/5]" or "[4/6]" in the C++ output
                const progressMatch = text.match(/\[(\d+)\/[56]\]/);
                if (progressMatch) {
                    const stepNumber = parseInt(progressMatch[1], 10);
                    // Stream just the step number to the browser immediately
                    res.write(JSON.stringify({ type: 'progress', step: stepNumber }) + '\n');
                }
            });

            humEngine.stderr.on('data', (data) => {
                console.error(`[C++ ERROR] ${data.toString()}`);
            });

            humEngine.on('close', (code) => {
                console.log(`\n[6/6 Handoff] C++ Engine exited with code ${code}`);

                const cleanupTempFiles = () => {
                    try {
                        if (fs.existsSync(uploadedFilePath)) fs.unlinkSync(uploadedFilePath);
                        if (fs.existsSync(standardWavPath)) fs.unlinkSync(standardWavPath);
                    } catch (err) {
                        console.error("[Warning] Failed to delete temp files:", err.message);
                    }
                };

                if (code === 0) {
                    const bpmMatch = engineTextOutput.match(/Calculated BPM:\s*([\d.]+)/);
                    const keyMatch = engineTextOutput.match(/Detected Global Key:\s*([^\n]+)/);
                    const chordMatch = engineTextOutput.match(/Harmonic Progression[^\n]*:[\s]*([\s\S]*?)={10,}/);
                    
                    // --- THE FINAL HANDOFF ---
                    // Stream the final completed payload, then officially close the connection
                    res.write(JSON.stringify({
                        type: 'complete',
                        success: true,
                        audioUrl: webAudioUrl,
                        analytics: {
                            bpm: bpmMatch ? parseFloat(bpmMatch[1]) : 0,
                            key: keyMatch ? keyMatch[1].trim() : "Unknown",
                            chords: chordMatch ? chordMatch[1].trim() : "No chords detected",
                            rawLogs: engineTextOutput 
                        }
                    }) + '\n');
                    res.end(); // Tell the browser the stream is finished
                } else {
                    res.write(JSON.stringify({ type: 'error', error: "C++ Engine encountered a fatal error." }) + '\n');
                    res.end();
                }
                
                cleanupTempFiles();
            });
        })
        .on('error', (err) => {
            console.error("\n[Error] FFmpeg failed to process the file:", err.message);
            res.status(500).json({ error: "Audio format conversion failed." });
            
            // Safe cleanup for failed FFmpeg runs
            try {
                if (fs.existsSync(uploadedFilePath)) fs.unlinkSync(uploadedFilePath);
            } catch (cleanupErr) {}
        })
        .save(standardWavPath);
});

const PORT = 3000;
app.listen(PORT, () => {
    console.log(`=== Node Bridge Running on http://localhost:${PORT} ===`);
});