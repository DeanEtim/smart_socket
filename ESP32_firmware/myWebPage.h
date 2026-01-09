const char myWebPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">

<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Wattmeter Dashboard</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #e3f2fd 0%, #f5f5f5 100%);
            min-height: 100vh;
            padding: 15px;
        }

        .container {
            max-width: 1000px;
            margin: 0 auto;
        }

        h1 {
            color: #1565c0;
            text-align: center;
            margin-bottom: 20px;
            font-size: clamp(20px, 4vw, 32px);
        }

        .status {
            text-align: center;
            padding: 8px;
            border-radius: 6px;
            margin-bottom: 15px;
            font-weight: 600;
            font-size: clamp(12px, 2.5vw, 14px);
        }

        .status.connected {
            background: #4caf50;
            color: white;
        }

        .status.disconnected {
            background: #f44336;
            color: white;
        }

        .gauges {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
            gap: 15px;
            margin-bottom: 20px;
        }

        .gauge-container {
            background: white;
            border-radius: 12px;
            padding: 15px;
            box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
            display: flex;
            flex-direction: column;
            align-items: center;
        }

        .gauge {
            position: relative;
            width: 140px;
            height: 140px;
        }

        .gauge-value {
            font-size: clamp(24px, 5vw, 32px);
            font-weight: bold;
            color: #1565c0;
            margin-bottom: 3px;
        }

        .gauge-unit {
            font-size: clamp(14px, 2.5vw, 18px);
            color: #666;
        }

        .gauge-label {
            margin-top: 10px;
            font-size: clamp(13px, 2vw, 16px);
            color: #333;
            font-weight: 600;
        }

        .energy-section {
            background: white;
            border-radius: 12px;
            padding: 20px;
            box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
        }

        .energy-display {
            text-align: center;
        }

        .energy-title {
            font-size: clamp(16px, 2.5vw, 20px);
            color: #1565c0;
            margin-bottom: 10px;
        }

        .energy-value {
            font-size: clamp(32px, 6vw, 48px);
            font-weight: bold;
            color: #1565c0;
        }

        .energy-unit {
            font-size: clamp(18px, 3vw, 28px);
            color: #666;
            margin-left: 8px;
        }

        @media (max-width: 768px) {
            body {
                padding: 10px;
            }

            h1 {
                margin-bottom: 15px;
            }

            .gauges {
                gap: 12px;
            }

            .gauge-container {
                padding: 12px;
            }

            .gauge {
                width: 120px;
                height: 120px;
            }

            .energy-section {
                padding: 15px;
            }
        }
    </style>
</head>

<body>
    <div class="container">
        <h1>Smart Wattmeter Dashboard</h1>

        <div id="status" class="status disconnected">Connecting to ESP32...</div>

        <div class="gauges">
            <div class="gauge-container">
                <canvas id="voltageGauge" class="gauge"></canvas>
                <div class="gauge-value" id="voltageValue">0</div>
                <div class="gauge-unit">V</div>
                <div class="gauge-label">RMS Voltage</div>
            </div>

            <div class="gauge-container">
                <canvas id="currentGauge" class="gauge"></canvas>
                <div class="gauge-value" id="currentValue">0</div>
                <div class="gauge-unit">A</div>
                <div class="gauge-label">RMS Current</div>
            </div>

            <div class="gauge-container">
                <canvas id="powerGauge" class="gauge"></canvas>
                <div class="gauge-value" id="powerValue">0</div>
                <div class="gauge-unit">W</div>
                <div class="gauge-label">Load</div>
            </div>
        </div>

        <div class="energy-section">
            <div class="energy-display">
                <div class="energy-title">Energy Consumption</div>
                <div>
                    <span class="energy-value" id="energyValue">0</span>
                    <span class="energy-unit">kWh</span>
                </div>
            </div>
        </div>
    </div>

    <script>
        let ws;

        // Gauge drawing function
        function drawGauge(canvasId, value, max, color) {
            const canvas = document.getElementById(canvasId);
            const ctx = canvas.getContext('2d');
            const centerX = canvas.width / 2;
            const centerY = canvas.height / 2;
            const radius = Math.min(centerX, centerY) - 8;

            ctx.clearRect(0, 0, canvas.width, canvas.height);

            // Background arc
            ctx.beginPath();
            ctx.arc(centerX, centerY, radius, 0.75 * Math.PI, 2.25 * Math.PI);
            ctx.lineWidth = 16;
            ctx.strokeStyle = '#e0e0e0';
            ctx.stroke();

            // Value arc
            const percentage = Math.min(value / max, 1);
            const endAngle = 0.75 * Math.PI + (percentage * 1.5 * Math.PI);

            ctx.beginPath();
            ctx.arc(centerX, centerY, radius, 0.75 * Math.PI, endAngle);
            ctx.lineWidth = 16;
            ctx.strokeStyle = color;
            ctx.lineCap = 'round';
            ctx.stroke();
        }

        // Initialize gauges
        function initGauges() {
            const canvases = ['voltageGauge', 'currentGauge', 'powerGauge'];
            canvases.forEach(id => {
                const canvas = document.getElementById(id);
                canvas.width = 140;
                canvas.height = 140;
            });

            drawGauge('voltageGauge', 0, 300, '#2196f3');
            drawGauge('currentGauge', 0, 20, '#2196f3');
            drawGauge('powerGauge', 0, 5000, '#2196f3');
        }

        // WebSocket connection
        function connectWebSocket() {
            ws = new WebSocket('ws://192.168.5.3:81');

            ws.onopen = function () {
                document.getElementById('status').className = 'status connected';
                document.getElementById('status').textContent = 'Connected to ESP32';
            };

            ws.onmessage = function (event) {
                try {
                    const data = JSON.parse(event.data);

                    // Update gauge values
                    document.getElementById('voltageValue').textContent = data.voltage.toFixed(1);
                    document.getElementById('currentValue').textContent = data.current.toFixed(2);
                    document.getElementById('powerValue').textContent = Math.round(data.power);
                    document.getElementById('energyValue').textContent = data.energy.toFixed(3);

                    // Update gauges
                    drawGauge('voltageGauge', data.voltage, 300, '#2196f3');
                    drawGauge('currentGauge', data.current, 20, '#2196f3');
                    drawGauge('powerGauge', data.power, 5000, '#2196f3');
                } catch (e) {
                    console.error('Error parsing data:', e);
                }
            };

            ws.onclose = function () {
                document.getElementById('status').className = 'status disconnected';
                document.getElementById('status').textContent = 'Reconnecting...';
                setTimeout(connectWebSocket, 3000);
            };

            ws.onerror = function (error) {
                console.error('WebSocket error:', error);
            };
        }

        // Initialize
        initGauges();
        connectWebSocket();
    </script>
</body>

</html>
)rawliteral";
