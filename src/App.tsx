import { useEffect, useMemo, useState } from 'react';
import {
  Activity,
  Bell,
  Bot,
  Check,
  ChevronRight,
  CircleAlert,
  CloudSun,
  Droplets,
  Leaf,
  LogOut,
  Power,
  RefreshCw,
  SlidersHorizontal,
  Sprout,
  Thermometer,
  Waves,
  Wifi,
  WifiOff,
  X,
} from 'lucide-react';
import { firebaseApi, type FirebaseValue, type IrrigationMode, type IrrigationStatus } from './api';
import { currentSession, logOut } from './auth';
import AuthScreen from './AuthScreen';

const defaultThreshold = 38;

function reading(value: number | null | undefined, suffix = ''): string {
  return value === null || value === undefined ? '--' : `${Math.round(value)}${suffix}`;
}

function property(value: FirebaseValue | undefined, key: string): FirebaseValue | undefined {
  if (value !== null && typeof value === 'object' && !Array.isArray(value)) return value[key];
  return undefined;
}

function numericValue(value: FirebaseValue | undefined): number | null {
  return typeof value === 'number' && Number.isFinite(value) ? value : null;
}

function percentageValue(value: FirebaseValue | undefined): number | null {
  const number = numericValue(value);
  return number !== null && number >= 0 && number <= 100 ? number : null;
}

function hasFreshFirebaseStatus(status: IrrigationStatus | undefined): boolean {
  if (!status || !Number.isFinite(status.updatedAt)) return false;
  const age = Date.now() - status.updatedAt;
  return age >= 0 && age < 30_000;
}

function clamp(value: number, min: number, max: number): number {
  return Math.min(Math.max(value, min), max);
}

type AiInsight = {
  badge: string;
  headline: string;
  summary: string;
  confidence: number;
  recommendations: string[];
  recommendedThreshold: number | null;
};

function buildAiInsight(input: {
  soil: number | null;
  tank: number | null;
  temperature: number | null;
  humidity: number | null;
  environmentAlerts: Array<{ summary: string; recommendation: string }>;
  threshold: number;
  pumpOn: boolean;
  tankLow: boolean;
  tankUnavailable: boolean;
}): AiInsight {
  const { soil, tank, temperature, humidity, environmentAlerts, threshold, pumpOn, tankLow, tankUnavailable } = input;

  if (tankUnavailable) {
    return {
      badge: 'Protective',
      headline: 'Tank-level reading unavailable',
      summary: 'The tank-level probe is missing or returning an invalid reading, so the firmware is keeping the pump locked off until it can verify the water reserve.',
      confidence: 95,
      recommendations: ['Check the tank probe and its empty/full calibration; the pump remains locked until a valid level is reported.'],
      recommendedThreshold: null,
    };
  }

  if (environmentAlerts.length > 0) {
    return {
      badge: 'Attention',
      headline: 'Environmental conditions need attention',
      summary: environmentAlerts.map((alert) => alert.summary).join(' '),
      confidence: 88,
      recommendations: environmentAlerts.map((alert) => alert.recommendation),
      recommendedThreshold: null,
    };
  }

  if (soil === null) {
    return {
      badge: 'Monitoring',
      headline: 'Collecting field data',
      summary: 'The soil sensor is not reporting a valid reading yet, so the AI is waiting for a fresh moisture sample before recommending action.',
      confidence: 58,
      recommendations: ['Check the soil probe wiring and placement.', 'Waiting for the next sensor refresh before dispatching a recommendation.'],
      recommendedThreshold: null,
    };
  }

  const recommendations: string[] = [];
  let confidence = 68;
  let headline = 'System is balanced';
  let summary = 'Current conditions are stable, and the irrigation schedule looks consistent with your target moisture level.';
  let recommendedThreshold = null;

  if (tankLow) {
    confidence += 12;
    headline = 'Water reserve is at risk';
    summary = 'The tank is below the safe operating threshold, so the system is protecting the pump from dry-running and the AI is prioritizing water conservation.';
    recommendations.push('Tank level is too low for safe watering. Keep the pump locked out until the reservoir is replenished.');
    recommendedThreshold = null;
  } else if (soil < threshold - 8) {
    confidence += 18;
    headline = 'Moisture is trending dry';
    summary = 'The soil is below your target by a meaningful margin, so the irrigation cycle should be allowed to run soon.';
    recommendations.push('Watering is likely needed soon. If the trend continues, the next cycle should start before the lower threshold is crossed again.');
    recommendedThreshold = clamp(threshold + 2, 15, 75);
  } else if (soil > threshold + 10 && !pumpOn) {
    confidence += 14;
    headline = 'Soil is still comfortably wet';
    summary = 'Moisture is above the target range, so the AI suggests delaying watering and conserving water for the next dry period.';
    recommendations.push('Hold off on irrigation for the next cycle unless the weather turns hotter or drier.');
    recommendedThreshold = clamp(threshold - 2, 15, 75);
  } else {
    recommendations.push('Current moisture is stable and close to your target, so routine watering remains appropriate.');
  }

  if (tank !== null && tank < 30) {
    recommendations.push('Reservoir reserve is moderately low, so the system should avoid prolonged watering bursts.');
    confidence += 8;
  }

  if (temperature !== null && temperature > 29 && humidity !== null && humidity < 45) {
    confidence += 10;
    recommendations.push('Warm, dry air is increasing evapotranspiration, so the garden may need more frequent checks during the day.');
  }

  if (soil < threshold - 3 && pumpOn) {
    recommendations.push('The pump is actively watering the bed, which matches the current dry trend. Keep monitoring until the soil returns to range.');
  }

  if (recommendations.length === 0) {
    recommendations.push('Keep the current schedule and monitor the next sensor refresh for drift.');
  }

  return {
    badge: tankLow ? 'Protective' : soil < threshold ? 'Active' : 'Stable',
    headline,
    summary,
    confidence: clamp(confidence, 55, 97),
    recommendations,
    recommendedThreshold,
  };
}

function App() {
  const [user, setUser] = useState<string | null>(() => currentSession());
  const [status, setStatus] = useState<IrrigationStatus>();
  const [connected, setConnected] = useState(false);
  const [pendingMode, setPendingMode] = useState<IrrigationMode | null>(null);
  const [pendingPump, setPendingPump] = useState<boolean | null>(null);
  const [threshold, setThreshold] = useState(defaultThreshold);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState('');
  const [lastUpdated, setLastUpdated] = useState<Date | null>(null);
  const [refreshTick, setRefreshTick] = useState(0);
  const [sensorData, setSensorData] = useState<FirebaseValue>();
  const [firebaseError, setFirebaseError] = useState('');
  const [activeTab, setActiveTab] = useState<'overview' | 'notifications'>('overview');
  const [dismissedNotifications, setDismissedNotifications] = useState<Set<string>>(() => new Set());

  useEffect(() => {
    if (!user) return;
    let alive = true;
    const refresh = async () => {
      try {
        const snapshot = await firebaseApi.snapshot();
        if (!alive) return;
        const next = snapshot.status ?? undefined;
        setStatus(next);
        setSensorData(snapshot.sensorData ?? undefined);
        if (next) setThreshold(next.moistureThreshold);
        const fresh = hasFreshFirebaseStatus(next);
        setConnected(fresh);
        setLastUpdated(fresh && next ? new Date(next.updatedAt) : null);
        setFirebaseError([
          snapshot.statusError,
          next ? (fresh ? null : 'ESP32 status is stale') : snapshot.statusError || 'Waiting for ESP32 status',
          snapshot.sensorDataError ? `sensorData: ${snapshot.sensorDataError}` : null,
        ].filter(Boolean).join(' · '));
      } catch (error) {
        if (alive) {
          setConnected(false);
          setFirebaseError(error instanceof Error ? error.message : 'Could not load Firebase data');
        }
      }
    };
    void refresh();
    const timer = window.setInterval(() => void refresh(), 5000);
    return () => {
      alive = false;
      window.clearInterval(timer);
    };
  }, [refreshTick, user]);

  useEffect(() => {
    if (!pendingMode) return;
    if (status?.mode === pendingMode) {
      setPendingMode(null);
      setMessage(`ESP32 confirmed ${pendingMode} mode`);
      return;
    }

    const timeout = window.setTimeout(() => {
      setPendingMode(null);
      setMessage(`ESP32 has not confirmed ${pendingMode} mode. Check the device connection and firmware.`);
    }, 15_000);
    return () => window.clearTimeout(timeout);
  }, [pendingMode, status?.mode]);

  useEffect(() => {
    if (pendingPump === null) return;
    if (status?.pumpOn === pendingPump) {
      setPendingPump(null);
      setMessage(`Firebase confirmed pump ${pendingPump ? 'started' : 'stopped'}`);
      return;
    }

    const timeout = window.setTimeout(() => {
      setPendingPump(null);
      setMessage(`Firebase has not confirmed the pump ${pendingPump ? 'start' : 'stop'} command. Check the device connection and firmware.`);
    }, 15_000);
    return () => window.clearTimeout(timeout);
  }, [pendingPump, status?.pumpOn]);

  const runCommand = async (command: () => Promise<unknown>, success: string) => {
    setBusy(true);
    setMessage('');
    try {
      await command();
      setMessage(`${success} in Firebase`);
      return true;
    } catch (error) {
      setMessage(error instanceof Error ? error.message : 'Command failed');
      return false;
    } finally {
      setBusy(false);
    }
  };

  const changeMode = (mode: IrrigationMode) => {
    setPendingMode(mode);
    void runCommand(() => firebaseApi.control(mode), `${mode} mode command sent; waiting for ESP32 confirmation`)
      .then((sent) => {
        if (!sent) setPendingMode(null);
      });
  };

  const saveThreshold = () => {
    void runCommand(() => firebaseApi.configure(threshold), 'Moisture threshold saved');
  };

  const pumpToggle = () => {
    if (!status) return;
    const requestedPump = !status.pumpOn;
    setPendingPump(requestedPump);
    void runCommand(
      () => firebaseApi.control('manual', requestedPump),
      `Pump ${requestedPump ? 'start' : 'stop'} command sent`,
    ).then((sent) => {
      if (!sent) setPendingPump(null);
    });
  };

  const firebaseSensorData = sensorData;
  const firebaseStatusLabel = connected ? 'DEVICE ONLINE' : status ? 'STATUS STALE' : 'NO STATUS RECORD';
  const firebaseSoil = numericValue(property(firebaseSensorData, 'soilMoisture'));
  const firebaseTank = percentageValue(property(firebaseSensorData, 'tankLevel'));
  const firebaseTemperature = numericValue(property(firebaseSensorData, 'temp'));
  const firebaseHumidity = percentageValue(property(firebaseSensorData, 'humidity'));
  const controllerSoil = status?.soilValid ? status.soilPercent : null;
  const controllerTank = status?.tankValid ? status.tankPercent : null;
  const tankRaw = status?.tankRaw ?? numericValue(property(firebaseSensorData, 'tankRaw'));
  const soil = firebaseSoil ?? controllerSoil;
  const soilPercent = firebaseSoil === null ? controllerSoil : firebaseSoil >= 0 && firebaseSoil <= 100 ? firebaseSoil : null;
  const tank = status?.tankValid ? firebaseTank ?? controllerTank : null;
  const temperature = firebaseTemperature ?? (status?.climateValid ? status.temperatureC : null);
  const humidity = firebaseHumidity ?? (status?.climateValid ? status.humidityPercent : null);
  const tankLow = tank !== null && tank < 18;
  const tankUnavailable = connected && tank === null;
  const tankWarning = tank !== null && tank < 20;
  const temperatureOutOfRange = temperature !== null && (temperature < 20 || temperature > 35);
  const temperatureAlertDetail = temperature !== null && temperature < 20
    ? `Temperature is below 20°C (${Math.round(temperature)}°C).`
    : `Temperature is above 35°C (${Math.round(temperature ?? 0)}°C).`;
  const humidityOutOfRange = humidity !== null && (humidity < 20 || humidity > 70);
  const soilVeryWet = soilPercent !== null && soilPercent > threshold + 10 && !status?.pumpOn;
  const environmentAlerts = [
    ...(temperatureOutOfRange ? [{
      summary: temperatureAlertDetail,
      recommendation: 'Check the garden temperature and protect plants from excessive heat or cold.',
    }] : []),
    ...(humidityOutOfRange ? [{
      summary: `Humidity is ${Math.round(humidity)}%, outside the 20–70% range.`,
      recommendation: 'Check airflow and plant conditions; very dry or humid air can stress the garden.',
    }] : []),
    ...(tankWarning ? [{
      summary: `Tank level is ${Math.round(tank)}%, below the 20% reserve alert.`,
      recommendation: 'Refill the reservoir soon. The pump remains locked out if the level falls below 18%.',
    }] : []),
  ];
  const notifications = [
    ...(!connected ? [{ id: 'controller', level: 'warning', title: 'Irrigation device is offline', detail: 'Check the device Wi-Fi connection and Firebase sync to restore live control.' }] : []),
    ...(tankUnavailable ? [{ id: 'tank-sensor', level: 'warning', title: 'Tank-level reading unavailable', detail: 'The firmware is keeping the pump locked off until the probe reports a valid tank level.' }] : []),
    ...(tankWarning ? [{ id: 'tank', level: tankLow ? 'critical' : 'warning', title: tankLow ? 'Water reserve is too low' : 'Water reserve is low', detail: tankLow ? `Tank level is ${Math.round(tank ?? 0)}%. Refill before running the pump.` : `Tank level is ${Math.round(tank ?? 0)}%, below the 20% reserve alert. Refill soon.` }] : []),
    ...(temperatureOutOfRange ? [{ id: 'temperature', level: 'warning', title: temperature !== null && temperature < 20 ? 'Temperature is below 20°C' : 'Temperature is above 35°C', detail: temperatureAlertDetail }] : []),
    ...(humidityOutOfRange ? [{ id: 'humidity', level: 'warning', title: 'Humidity is outside range', detail: `Humidity is ${Math.round(humidity ?? 0)}%. Expected range: 20–70%.` }] : []),
    ...(soilPercent !== null && soilPercent < threshold ? [{ id: 'soil', level: 'warning', title: 'Soil moisture is below target', detail: `Moisture is ${Math.round(soilPercent)}%, below your ${threshold}% target.` }] : []),
    ...(soilVeryWet ? [{ id: 'soil-wet', level: 'warning', title: 'Soil moisture is well above target', detail: `Moisture is ${Math.round(soilPercent)}%, more than 10 points above your ${threshold}% target. Consider delaying the next watering cycle.` }] : []),
    ...(connected && soilPercent === null ? [{ id: 'soil-sensor', level: 'warning', title: 'Soil sensor has no valid reading', detail: 'Check the probe connection; automatic irrigation needs a valid moisture value.' }] : []),
    ...(firebaseError ? [{ id: 'firebase', level: 'warning', title: 'Firebase data is unavailable', detail: firebaseError }] : []),
    ...(status?.pumpOn ? [{ id: 'pump', level: 'info', title: 'Watering is in progress', detail: 'The irrigation pump is currently running.' }] : []),
  ];
  const notificationIds = notifications.map((notification) => notification.id).join('|');
  useEffect(() => {
    const activeIds = new Set(notificationIds.split('|').filter(Boolean));
    setDismissedNotifications((dismissed) => {
      const next = new Set([...dismissed].filter((id) => activeIds.has(id)));
      return next.size === dismissed.size ? dismissed : next;
    });
  }, [notificationIds]);
  const visibleNotifications = notifications.filter((notification) => !dismissedNotifications.has(notification.id));
  const attentionCount = visibleNotifications.filter((notification) => notification.level !== 'info').length;

  const aiInsight = useMemo(
    () =>
      buildAiInsight({
        soil: soilPercent,
        tank,
        temperature,
        humidity,
        environmentAlerts,
        threshold,
        pumpOn: Boolean(status?.pumpOn),
        tankLow,
        tankUnavailable,
      }),
    [environmentAlerts, humidity, soilPercent, status?.pumpOn, tank, tankLow, tankUnavailable, temperature, threshold],
  );

  const applyAiThreshold = () => {
    if (aiInsight.recommendedThreshold === null) return;
    setThreshold(aiInsight.recommendedThreshold);
    void runCommand(() => firebaseApi.configure(aiInsight.recommendedThreshold!), 'AI threshold adjusted');
  };

  const handleLogout = () => {
    logOut();
    setUser(null);
  };

  if (!user) return <AuthScreen onAuthenticated={setUser} />;

  return (
    <main className="app-shell">
      <aside className="rail" aria-label="Main navigation">
        <a className="brand-mark" href="#overview" aria-label="aquaSense home"><Sprout size={22} strokeWidth={2.1} /></a>
        <div className="rail-rule" />
        <a className={`rail-link ${activeTab === 'overview' ? 'active' : ''}`} href="#overview" aria-label="Overview" title="Overview" onClick={() => setActiveTab('overview')}><Activity size={19} /></a>
        <a className="rail-link" href="#sensors" aria-label="Sensors" title="Sensors"><Waves size={19} /></a>
        <a className="rail-link" href="#controls" aria-label="Controls" title="Controls"><SlidersHorizontal size={19} /></a>
        <button className={`rail-link notification-link ${activeTab === 'notifications' ? 'active' : ''}`} type="button" aria-label={attentionCount ? `Notifications, ${attentionCount} active alerts` : 'Notifications'} title="Notifications" aria-current={activeTab === 'notifications' ? 'page' : undefined} onClick={() => setActiveTab('notifications')}>
          <Bell size={19} />
          {attentionCount > 0 && <span className="notification-count">{attentionCount > 9 ? '9+' : attentionCount}</span>}
        </button>
        <div className="rail-bottom"><span className={`connection-dot ${connected ? 'online' : ''}`} /></div>
      </aside>

      <section className="workspace" id="overview">
        <header className="topbar">
          <div className="brand-lockup"><span className="brand-name">aquaSense</span><span className="brand-divider" /><span className="site-name">GARDEN / NORTH BED</span></div>
          <div className="topbar-right">
            <span className={`connection-state ${connected ? 'is-online' : ''}`}>
              {connected ? <Wifi size={15} /> : <WifiOff size={15} />}
              {connected ? 'Device live' : 'Device offline'}
            </span>
            <span className="topbar-date">{lastUpdated ? `Updated ${lastUpdated.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}` : 'Waiting for data'}</span>
            <div className="account-control"><span>{user}</span><button onClick={handleLogout} title="Sign out" aria-label="Sign out"><LogOut size={15} /></button></div>
          </div>
        </header>

        <div className="content">
          {activeTab === 'notifications' ? (
            <section className="notifications-view" aria-labelledby="notifications-title">
              <div className="notifications-heading">
                <div>
                  <span className="eyebrow"><span className="eyebrow-line" /> SYSTEM FEED</span>
                  <h1 id="notifications-title">Notifications</h1>
                  <p className="intro-copy">Live alerts from your irrigation system.</p>
                </div>
                <div className={`notification-summary ${attentionCount ? 'has-alerts' : ''}`}>
                  <strong>{attentionCount}</strong>
                  <span>{attentionCount === 1 ? 'needs attention' : 'need attention'}</span>
                </div>
              </div>
              <div className="notification-list" aria-live="polite">
                {visibleNotifications.length ? visibleNotifications.map((notification) => (
                  <article className={`notification-item notification-${notification.level}`} key={notification.id}>
                    <span className="notification-symbol">{notification.level === 'info' ? <Droplets size={17} /> : <CircleAlert size={17} />}</span>
                    <div className="notification-copy">
                      <div className="notification-title-row">
                        <h2>{notification.title}</h2>
                        <div className="notification-actions">
                          <span>{notification.level === 'critical' ? 'URGENT' : notification.level === 'warning' ? 'ATTENTION' : 'UPDATE'}</span>
                          <button type="button" className="dismiss-notification" aria-label={`Dismiss ${notification.title}`} title="Dismiss notification" onClick={() => setDismissedNotifications((dismissed) => new Set(dismissed).add(notification.id))}><X size={15} /></button>
                        </div>
                      </div>
                      <p>{notification.detail}</p>
                    </div>
                  </article>
                )) : (
                  <div className="notifications-empty">
                    <span><Check size={18} /></span>
                    <div><strong>{notifications.length ? 'You’re all caught up' : 'All clear'}</strong><p>{notifications.length ? 'Dismissed alerts will return if their conditions change.' : 'No active alerts. Your system is operating within its current limits.'}</p></div>
                  </div>
                )}
              </div>
              <button className="back-overview" type="button" onClick={() => setActiveTab('overview')}><Activity size={15} /> Back to overview</button>
            </section>
          ) : (
            <>
          <section className="intro-row">
            <div>
              <div className="eyebrow"><span className="eyebrow-line" /> LIVE SYSTEM</div>
              <h1>Good morning,<br /><em>let’s grow.</em></h1>
              <p className="intro-copy">Your garden at a glance. Soil, water, and climate in one place.</p>
            </div>
            <div className={`system-badge ${connected ? 'badge-live' : ''}`}>
              <span className="badge-icon">{connected ? <Check size={16} /> : <CircleAlert size={16} />}</span>
              <span><strong>{connected ? (status?.pumpOn ? 'Watering now' : tankUnavailable ? 'Pump locked' : 'System ready') : 'No connection'}</strong><small>{connected ? (tankUnavailable ? 'Check tank-level probe' : `Irrigation ${status?.mode ?? 'auto'} mode`) : 'Check device Wi-Fi and Firebase'}</small></span>
            </div>
          </section>

          <section className="metric-strip" id="sensors" aria-label="Live sensor readings">
            <article className="metric metric-soil">
              <div className="metric-heading"><span className="metric-icon"><Leaf size={17} /></span><span>SOIL MOISTURE</span><span className="live-tick" /></div>
              <div className="metric-value">{reading(soil, soilPercent === null ? '' : '%')}</div>
              <div className="metric-foot"><span>{soil === null ? 'Awaiting sensor' : soilPercent === null ? 'Raw Firebase reading' : soilPercent < threshold ? 'Below target' : 'Within target'}</span><span>{soilPercent === null ? 'Sensor value' : `Target ${threshold}%`}</span></div>
              {soilPercent !== null && <div className="meter"><span style={{ width: `${soilPercent}%` }} /></div>}
            </article>
            <article className={`metric metric-tank ${tankLow ? 'metric-warning' : ''}`}>
              <div className="metric-heading"><span className="metric-icon"><Droplets size={17} /></span><span>WATER RESERVE</span><span className="live-tick" /></div>
              <div className="metric-value">{reading(tank, '%')}</div>
              <div className="metric-foot"><span>{tankLow ? 'Refill soon' : tank === null ? tankRaw === null || tankRaw === undefined ? 'Sensor check' : `Raw ADC ${Math.round(tankRaw)}` : 'Tank level'}</span><span>{tank === null ? 'Calibrate to unlock' : 'Low at 18%'}</span></div>
              <div className="meter"><span style={{ width: `${tank ?? 0}%` }} /></div>
            </article>
            <article className="metric metric-climate">
              <div className="metric-heading"><span className="metric-icon"><CloudSun size={17} /></span><span>MICROCLIMATE</span><span className="live-tick" /></div>
              <div className="climate-values"><div><strong>{reading(temperature, '°')}</strong><span>C</span><small><Thermometer size={13} /> TEMP</small></div><i /><div><strong>{reading(humidity, '%')}</strong><small><Waves size={13} /> HUMIDITY</small></div></div>
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
                {aiInsight.recommendations.map((item) => (
                  <li key={item}>{item}</li>
                ))}
              </ul>
            </div>
            {aiInsight.recommendedThreshold !== null && (
              <button className="ai-apply" onClick={applyAiThreshold} disabled={!connected || busy}>
                Apply AI target: {aiInsight.recommendedThreshold}% moisture
              </button>
            )}
          </section>

          <div className="section-heading"><div><span className="eyebrow">CONTROL DECK</span><h2>Water, thoughtfully.</h2></div><button className="icon-button" title="Refresh readings" aria-label="Refresh readings" onClick={() => setRefreshTick((tick) => tick + 1)}><RefreshCw size={16} /></button></div>

          <section className="control-grid" id="controls">
            <article className="control-panel mode-panel">
              <div className="panel-top"><div><span className="panel-kicker">01 / IRRIGATION</span><h3>Choose a rhythm</h3></div><span className="panel-icon"><Sprout size={18} /></span></div>
              <p className="panel-copy">Let soil moisture guide the pump, or take the wheel yourself.</p>
              <div className="mode-switch" role="group" aria-label="Irrigation mode">
                <button className={(pendingMode ?? status?.mode ?? 'auto') === 'auto' ? 'selected' : ''} aria-pressed={(pendingMode ?? status?.mode ?? 'auto') === 'auto'} onClick={() => changeMode('auto')}><Activity size={15} /> Auto</button>
                <button className={(pendingMode ?? status?.mode) === 'manual' ? 'selected' : ''} aria-pressed={(pendingMode ?? status?.mode) === 'manual'} onClick={() => changeMode('manual')}><SlidersHorizontal size={15} /> Manual</button>
              </div>
              <div className="panel-divider" />
              <div className="pump-row"><div><strong>Water pump</strong><span>{status?.pumpOn ? 'Running' : 'Standby'}{pendingPump !== null ? ' · Awaiting Firebase' : tankLow ? ' · Tank low' : tankUnavailable ? ' · Tank sensor required' : ''}</span></div><button className={`pump-button ${status?.pumpOn ? 'pump-active' : ''}`} onClick={pumpToggle} disabled={busy || pendingPump !== null || !status || (!status.pumpOn && (tankLow || tank === null))} aria-label={status?.pumpOn ? 'Stop pump' : 'Start pump'}><Power size={17} /><span>{status?.pumpOn ? 'Stop' : 'Start'}</span></button></div>
              {(tankLow || tankUnavailable) && <p className="safety-note"><CircleAlert size={14} /> {tankUnavailable ? 'Pump locked until the tank probe reports a valid level.' : 'Pump locked until the reservoir is refilled.'}</p>}
            </article>

            <article className="control-panel threshold-panel">
              <div className="panel-top"><div><span className="panel-kicker">02 / SOIL TARGET</span><h3>Set the dry point</h3></div><span className="panel-icon panel-icon-lime"><Droplets size={18} /></span></div>
              <p className="panel-copy">Auto mode starts watering when moisture falls below your target.</p>
              <div className="threshold-readout"><strong>{threshold}<small>%</small></strong><span>moisture threshold</span></div>
              <input className="threshold-slider" type="range" min="15" max="75" step="1" value={threshold} style={{ background: `linear-gradient(90deg, #95b86c 0%, #95b86c ${((threshold - 15) / 60) * 100}%, #dfe4d7 ${((threshold - 15) / 60) * 100}%, #dfe4d7 100%)` }} onChange={(event) => setThreshold(Number(event.target.value))} aria-label="Moisture threshold" />
              <div className="slider-labels"><span>DRIER</span><span>WETTER</span></div>
              <button className="save-button" onClick={saveThreshold} disabled={!connected || busy}><span>{busy ? 'Saving…' : 'Save threshold'}</span><ChevronRight size={16} /></button>
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
          )}
          {message && <div className="toast" role="status">{message}</div>}
          <footer className="footer"><span>aquaSense <i>·</i> FIREBASE CONTROL</span><span><span className={`footer-dot ${connected ? 'online' : ''}`} />{connected ? 'DATA STREAM ACTIVE' : 'RECONNECTING'} <button onClick={() => setRefreshTick((tick) => tick + 1)} aria-label="Retry connection"><RefreshCw size={12} /></button></span></footer>
        </div>
      </section>
    </main>
  );
}

export default App;
