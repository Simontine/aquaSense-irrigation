import { Activity, CircleAlert, Droplets, Timer } from 'lucide-react';
import type { IrrigationHistory, SensorHistoryPoint } from '../history';

interface HistoryPageProps {
  history: IrrigationHistory;
  historyError: string | null;
  onBack: () => void;
}

function formatTime(at: number): string {
  return new Date(at).toLocaleString([], {
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
  });
}

function formatRuntime(milliseconds: number): string {
  const minutes = Math.floor(milliseconds / 60_000);
  const hours = Math.floor(minutes / 60);
  const remainingMinutes = minutes % 60;
  return hours > 0 ? `${hours}h ${remainingMinutes}m` : `${remainingMinutes}m`;
}

function chartPath(points: SensorHistoryPoint[], valueFor: (point: SensorHistoryPoint) => number | null): string {
  const valid = points
    .map((point) => ({ at: point.at, value: valueFor(point) }))
    .filter((point): point is { at: number; value: number } => point.value !== null);
  if (!valid.length) return '';

  const firstAt = valid[0].at;
  const lastAt = valid[valid.length - 1].at;
  return valid.map((point, index) => {
    const x = lastAt === firstAt ? 300 : ((point.at - firstAt) / (lastAt - firstAt)) * 600;
    const y = 170 - (point.value / 100) * 150;
    return `${index === 0 ? 'M' : 'L'} ${x.toFixed(1)} ${y.toFixed(1)}`;
  }).join(' ');
}

function HistoryChart({ title, icon, points, valueFor, color, emptyLabel }: {
  title: string;
  icon: 'soil' | 'tank';
  points: SensorHistoryPoint[];
  valueFor: (point: SensorHistoryPoint) => number | null;
  color: string;
  emptyLabel: string;
}) {
  const path = chartPath(points, valueFor);
  const latest = [...points].reverse().find((point) => valueFor(point) !== null);
  const latestValue = latest ? valueFor(latest) : null;

  return (
    <article className="history-chart-card">
      <div className="history-chart-heading">
        <span className="history-chart-icon">{icon === 'soil' ? <Activity size={17} /> : <Droplets size={17} />}</span>
        <div><h2>{title}</h2><p>Last 7 days</p></div>
        <strong>{latestValue === null ? '--' : `${Math.round(latestValue)}%`}</strong>
      </div>
      {path ? (
        <div className="history-chart">
          <div className="history-chart-axis" aria-hidden="true"><span>100%</span><span>50%</span><span>0%</span></div>
          <svg viewBox="0 0 600 180" role="img" aria-label={`${title} over the last seven days`}>
            <path className="history-chart-grid" d="M 0 20 H 600 M 0 95 H 600 M 0 170 H 600" />
            <path d={path} fill="none" stroke={color} strokeWidth="3" strokeLinecap="round" strokeLinejoin="round" />
          </svg>
        </div>
      ) : (
        <div className="history-chart-empty">{emptyLabel}</div>
      )}
      <div className="history-chart-dates">
        <span>{points[0] ? formatTime(points[0].at) : 'No readings yet'}</span>
        <span>{points.length > 1 ? formatTime(points[points.length - 1].at) : ''}</span>
      </div>
    </article>
  );
}

function HistoryPage({ history, historyError, onBack }: HistoryPageProps) {
  const events = [...history.pumpEvents].reverse().slice(0, 12);

  return (
    <section className="history-view" aria-labelledby="history-title">
      <div className="history-heading">
        <div>
          <span className="eyebrow"><span className="eyebrow-line" /> IRRIGATION RECORD</span>
          <h1 id="history-title">History &amp; usage</h1>
          <p className="intro-copy">See how soil and water reserves change alongside pump activity.</p>
        </div>
        <div className="history-storage-note">Stored in this browser</div>
      </div>

      {historyError && <p className="history-error" role="alert"><CircleAlert size={15} />{historyError}</p>}

      <div className="history-summary">
        <article>
          <span className="history-summary-icon"><Timer size={17} /></span>
          <div><small>RUNTIME SINCE TRACKING BEGAN</small><strong>{formatRuntime(history.observedRuntimeMs)}</strong></div>
        </article>
        <article>
          <span className="history-summary-icon"><Activity size={17} /></span>
          <div><small>SENSOR SAMPLES</small><strong>{history.readings.length}</strong></div>
        </article>
        <article>
          <span className="history-summary-icon"><Droplets size={17} /></span>
          <div><small>PUMP STATE CHANGES</small><strong>{history.pumpEvents.filter((event) => event.kind !== 'observed').length}</strong></div>
        </article>
      </div>

      <div className="history-chart-grid">
        <HistoryChart
          title="Soil moisture"
          icon="soil"
          points={history.readings}
          valueFor={(point) => point.soilPercent}
          color="#397b4f"
          emptyLabel="Valid soil readings will appear here as the dashboard records them."
        />
        <HistoryChart
          title="Water reserve"
          icon="tank"
          points={history.readings}
          valueFor={(point) => point.tankPercent}
          color="#559b9b"
          emptyLabel="Valid tank readings will appear here as the dashboard records them."
        />
      </div>

      <section className="pump-history" aria-labelledby="pump-history-title">
        <div className="pump-history-heading">
          <div><span className="eyebrow">PUMP ACTIVITY</span><h2 id="pump-history-title">Recent state changes</h2></div>
          <span>Last 7 days</span>
        </div>
        {events.length ? (
          <ol className="pump-event-list">
            {events.map((event) => (
              <li key={`${event.at}-${event.kind}`}>
                <span className={`pump-event-dot ${event.pumpOn ? 'pump-event-on' : ''}`} />
                <div><strong>{event.kind === 'observed' ? `Pump observed ${event.pumpOn ? 'running' : 'stopped'}` : `Pump ${event.pumpOn ? 'started' : 'stopped'}`}</strong><time dateTime={new Date(event.at).toISOString()}>{formatTime(event.at)}</time></div>
                <span className={event.pumpOn ? 'pump-event-state-on' : ''}>{event.pumpOn ? 'ON' : 'OFF'}</span>
              </li>
            ))}
          </ol>
        ) : (
          <p className="pump-history-empty">Pump activity will be recorded when a fresh device status is received.</p>
        )}
      </section>

      <p className="history-disclaimer">
        History is collected while this dashboard is open and is saved only in this browser. Pump runtime counts observed intervals, not calibrated water volume; missed/offline intervals are excluded.
      </p>
      <button className="back-overview" type="button" onClick={onBack}><Activity size={15} /> Back to overview</button>
    </section>
  );
}

export default HistoryPage;
