import {
  Activity,
  Bot,
  Check,
  ChevronRight,
  CircleAlert,
  CloudSun,
  Droplets,
  Leaf,
  Power,
  RefreshCw,
  SlidersHorizontal,
  Sprout,
  Thermometer,
  Waves,
  Wifi,
} from 'lucide-react';
import type { OverviewPageProps } from '../types/dashboard';

function reading(value: number | null, suffix = ''): string {
  return value === null ? '--' : `${Math.round(value)}${suffix}`;
}

function OverviewPage({
  connected,
  status,
  soil,
  soilPercent,
  tank,
  tankRaw,
  temperature,
  humidity,
  tankLow,
  tankUnavailable,
  threshold,
  pendingMode,
  pendingPump,
  busy,
  aiInsight,
  firebaseStatusLabel,
  lastUpdated,
  sensorData,
  onRefresh,
  onChangeMode,
  onPumpToggle,
  onThresholdChange,
  onSaveThreshold,
  onApplyAiThreshold,
}: OverviewPageProps) {
  return (
    <>
      <section className="intro-row">
        <div>
          <div className="eyebrow"><span className="eyebrow-line" /> LIVE SYSTEM</div>
          <h1>Good morning,<br /><em>let’s grow.</em></h1>
          <p className="intro-copy">Your garden at a glance. Soil, water, and climate in one place.</p>
        </div>
        <div className={`system-badge ${connected ? 'badge-live' : ''}`}>
          <span className="badge-icon">{connected ? <Check size={16} /> : <CircleAlert size={16} />}</span>
          <span>
            <strong>{connected ? (status?.pumpOn ? 'Watering now' : tankUnavailable ? 'Pump locked' : 'System ready') : 'No connection'}</strong>
            <small>{connected ? (tankUnavailable ? 'Check tank-level probe' : `Irrigation ${status?.mode ?? 'auto'} mode`) : 'Check device Wi-Fi and Firebase'}</small>
          </span>
        </div>
      </section>

      <section className="metric-strip" id="sensors" aria-label="Live sensor readings">
        <article className="metric metric-soil">
          <div className="metric-heading"><span className="metric-icon"><Leaf size={17} /></span><span>SOIL MOISTURE</span><span className="live-tick" /></div>
          <div className="metric-value">{reading(soil, soilPercent === null ? '' : '%')}</div>
          <div className="metric-foot">
            <span>{soil === null ? 'Awaiting sensor' : soilPercent === null ? 'Raw Firebase reading' : soilPercent < threshold ? 'Below target' : 'Within target'}</span>
            <span>{soilPercent === null ? 'Sensor value' : `Target ${threshold}%`}</span>
          </div>
          {soilPercent !== null && <div className="meter"><span style={{ width: `${soilPercent}%` }} /></div>}
        </article>
        <article className={`metric metric-tank ${tankLow ? 'metric-warning' : ''}`}>
          <div className="metric-heading"><span className="metric-icon"><Droplets size={17} /></span><span>WATER RESERVE</span><span className="live-tick" /></div>
          <div className="metric-value">{reading(tank, '%')}</div>
          <div className="metric-foot">
            <span>{tankLow ? 'Refill soon' : tank === null ? tankRaw === null ? 'Sensor check' : `Raw ADC ${Math.round(tankRaw)}` : 'Tank level'}</span>
            <span>{tank === null ? 'Calibrate to unlock' : 'Low at 18%'}</span>
          </div>
          <div className="meter"><span style={{ width: `${tank ?? 0}%` }} /></div>
        </article>
        <article className="metric metric-climate">
          <div className="metric-heading"><span className="metric-icon"><CloudSun size={17} /></span><span>MICROCLIMATE</span><span className="live-tick" /></div>
          <div className="climate-values">
            <div><strong>{reading(temperature, '°')}</strong><span>C</span><small><Thermometer size={13} /> TEMP</small></div>
            <i />
            <div><strong>{reading(humidity, '%')}</strong><small><Waves size={13} /> HUMIDITY</small></div>
          </div>
          <div className="metric-foot"><span>{temperature !== null || humidity !== null ? 'Air conditions' : 'Awaiting sensor'}</span><span>Live readings</span></div>
        </article>
      </section>

      <section className="ai-panel" aria-live="polite">
        <div className="ai-header">
          <div>
            <span className="eyebrow">aquaSense AI</span>
            <h2>Adaptive irrigation guidance</h2>
          </div>
          <div className="ai-confidence">
            <span className="ai-bot"><Bot size={15} /></span>
            <strong>{aiInsight.confidence}%</strong>
            <small>confidence</small>
          </div>
        </div>
        <div className="ai-content">
          <div className="ai-callout">
            <span className={`ai-badge ${aiInsight.badge.toLowerCase().replace(/\s+/g, '-')}`}>{aiInsight.badge}</span>
            <h3>{aiInsight.headline}</h3>
            <p>{aiInsight.summary}</p>
          </div>
          <ul className="ai-recommendations">
            {aiInsight.recommendations.map((item) => <li key={item}>{item}</li>)}
          </ul>
        </div>
        {aiInsight.recommendedThreshold !== null && (
          <button className="ai-apply" onClick={onApplyAiThreshold} disabled={!connected || busy}>
            Apply AI target: {aiInsight.recommendedThreshold}% moisture
          </button>
        )}
      </section>

      <div className="section-heading">
        <div><span className="eyebrow">CONTROL DECK</span><h2>Water, thoughtfully.</h2></div>
        <button className="icon-button" title="Refresh readings" aria-label="Refresh readings" onClick={onRefresh}><RefreshCw size={16} /></button>
      </div>

      <section className="control-grid" id="controls">
        <article className="control-panel mode-panel">
          <div className="panel-top">
            <div><span className="panel-kicker">01 / IRRIGATION</span><h3>Choose a rhythm</h3></div>
            <span className="panel-icon"><Sprout size={18} /></span>
          </div>
          <p className="panel-copy">Let soil moisture guide the pump, or take the wheel yourself.</p>
          <div className="mode-switch" role="group" aria-label="Irrigation mode">
            <button
              className={(pendingMode ?? status?.mode ?? 'auto') === 'auto' ? 'selected' : ''}
              aria-pressed={(pendingMode ?? status?.mode ?? 'auto') === 'auto'}
              onClick={() => onChangeMode('auto')}
            >
              <Activity size={15} /> Auto
            </button>
            <button
              className={(pendingMode ?? status?.mode) === 'manual' ? 'selected' : ''}
              aria-pressed={(pendingMode ?? status?.mode) === 'manual'}
              onClick={() => onChangeMode('manual')}
            >
              <SlidersHorizontal size={15} /> Manual
            </button>
          </div>
          <div className="panel-divider" />
          <div className="pump-row">
            <div>
              <strong>Water pump</strong>
              <span>{status?.pumpOn ? 'Running' : 'Standby'}{pendingPump !== null ? ' · Awaiting Firebase' : tankLow ? ' · Tank low' : tankUnavailable ? ' · Tank sensor required' : ''}</span>
            </div>
            <button
              className={`pump-button ${status?.pumpOn ? 'pump-active' : ''}`}
              onClick={onPumpToggle}
              disabled={busy || pendingPump !== null || !status || (!status.pumpOn && (tankLow || tank === null))}
              aria-label={status?.pumpOn ? 'Stop pump' : 'Start pump'}
            >
              <Power size={17} /><span>{status?.pumpOn ? 'Stop' : 'Start'}</span>
            </button>
          </div>
          {(tankLow || tankUnavailable) && (
            <p className="safety-note">
              <CircleAlert size={14} /> {tankUnavailable ? 'Pump locked until the tank probe reports a valid level.' : 'Pump locked until the reservoir is refilled.'}
            </p>
          )}
        </article>

        <article className="control-panel threshold-panel">
          <div className="panel-top">
            <div><span className="panel-kicker">02 / SOIL TARGET</span><h3>Set the dry point</h3></div>
            <span className="panel-icon panel-icon-lime"><Droplets size={18} /></span>
          </div>
          <p className="panel-copy">Auto mode starts watering when moisture falls below your target.</p>
          <div className="threshold-readout"><strong>{threshold}<small>%</small></strong><span>moisture threshold</span></div>
          <input
            className="threshold-slider"
            type="range"
            min="15"
            max="75"
            step="1"
            value={threshold}
            style={{ background: `linear-gradient(90deg, #95b86c 0%, #95b86c ${((threshold - 15) / 60) * 100}%, #dfe4d7 ${((threshold - 15) / 60) * 100}%, #dfe4d7 100%)` }}
            onChange={(event) => onThresholdChange(Number(event.target.value))}
            aria-label="Moisture threshold"
          />
          <div className="slider-labels"><span>DRIER</span><span>WETTER</span></div>
          <button className="save-button" onClick={onSaveThreshold} disabled={!connected || busy}>
            <span>{busy ? 'Saving…' : 'Save threshold'}</span><ChevronRight size={16} />
          </button>
        </article>
      </section>

      <section className="connection-panel">
        <div className="connection-panel-icon"><Wifi size={17} /></div>
        <div className="connection-info"><strong>Cloud connection</strong><span>ESP32 syncs readings and commands through Firebase Realtime Database</span></div>
      </section>
      <section className="firebase-panel" aria-label="Firebase data">
        <div className="firebase-heading">
          <div><span className="eyebrow">REALTIME DATABASE</span><h2>Firebase view</h2></div>
          <span>{firebaseStatusLabel}{lastUpdated ? ` · ${lastUpdated.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })}` : ''}</span>
        </div>
        <pre className="firebase-json" aria-live="polite">{JSON.stringify({ 'irrigation/status': status ?? null, sensorData: sensorData ?? null }, null, 2)}</pre>
      </section>
    </>
  );
}

export default OverviewPage;
