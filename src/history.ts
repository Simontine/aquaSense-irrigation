export interface SensorHistoryPoint {
  at: number;
  soilPercent: number | null;
  tankPercent: number | null;
}

export interface PumpHistoryEvent {
  at: number;
  pumpOn: boolean;
  kind: 'observed' | 'started' | 'stopped';
}

export interface IrrigationHistory {
  readings: SensorHistoryPoint[];
  pumpEvents: PumpHistoryEvent[];
  observedRuntimeMs: number;
  lastObservedAt: number | null;
  lastPumpOn: boolean | null;
}

interface HistoryObservation {
  at: number;
  connected: boolean;
  pumpOn: boolean;
  soilPercent: number | null;
  tankPercent: number | null;
}

const storageKey = 'aquasense.irrigationHistory.v1';
const retentionMs = 7 * 24 * 60 * 60 * 1000;
const sampleIntervalMs = 60 * 1000;
const maxReadings = 10_080;
const maxPumpEvents = 500;
const maxRuntimeGapMs = 15_000;

export function emptyHistory(): IrrigationHistory {
  return {
    readings: [],
    pumpEvents: [],
    observedRuntimeMs: 0,
    lastObservedAt: null,
    lastPumpOn: null,
  };
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function isNullablePercentage(value: unknown): value is number | null {
  return value === null || (typeof value === 'number' && Number.isFinite(value) && value >= 0 && value <= 100);
}

function parseHistory(value: unknown): IrrigationHistory {
  if (!isRecord(value) || !Array.isArray(value.readings) || !Array.isArray(value.pumpEvents)) {
    throw new Error('Stored irrigation history has an invalid format');
  }

  const readings = value.readings.filter((point): point is SensorHistoryPoint => (
    isRecord(point)
    && typeof point.at === 'number'
    && Number.isFinite(point.at)
    && isNullablePercentage(point.soilPercent)
    && isNullablePercentage(point.tankPercent)
  ));
  const pumpEvents = value.pumpEvents.filter((event): event is PumpHistoryEvent => (
    isRecord(event)
    && typeof event.at === 'number'
    && Number.isFinite(event.at)
    && typeof event.pumpOn === 'boolean'
    && (event.kind === 'observed' || event.kind === 'started' || event.kind === 'stopped')
  ));

  if (
    typeof value.observedRuntimeMs !== 'number'
    || !Number.isFinite(value.observedRuntimeMs)
    || value.observedRuntimeMs < 0
    || !(value.lastObservedAt === null || (typeof value.lastObservedAt === 'number' && Number.isFinite(value.lastObservedAt)))
    || !(value.lastPumpOn === null || typeof value.lastPumpOn === 'boolean')
  ) {
    throw new Error('Stored irrigation history has invalid tracking data');
  }

  return {
    readings,
    pumpEvents,
    observedRuntimeMs: value.observedRuntimeMs,
    lastObservedAt: value.lastObservedAt,
    lastPumpOn: value.lastPumpOn,
  };
}

export function loadHistory(): { history: IrrigationHistory; error: string | null } {
  try {
    const stored = window.localStorage.getItem(storageKey);
    return {
      history: stored === null ? emptyHistory() : parseHistory(JSON.parse(stored)),
      error: null,
    };
  } catch (error) {
    return {
      history: emptyHistory(),
      error: error instanceof Error ? `History could not be loaded: ${error.message}` : 'History could not be loaded',
    };
  }
}

export function saveHistory(history: IrrigationHistory): void {
  window.localStorage.setItem(storageKey, JSON.stringify(history));
}

export function recordObservation(history: IrrigationHistory, observation: HistoryObservation): IrrigationHistory {
  const { at, connected, pumpOn, soilPercent, tankPercent } = observation;
  if (!connected || !Number.isFinite(at) || at <= (history.lastObservedAt ?? 0)) return history;

  const readings = [...history.readings];
  const pumpEvents = [...history.pumpEvents];
  let observedRuntimeMs = history.observedRuntimeMs;

  if (history.lastObservedAt === null) {
    pumpEvents.push({ at, pumpOn, kind: 'observed' });
  } else if (history.lastPumpOn !== pumpOn) {
    pumpEvents.push({ at, pumpOn, kind: pumpOn ? 'started' : 'stopped' });
  } else if (pumpOn) {
    const elapsed = at - history.lastObservedAt;
    if (elapsed <= maxRuntimeGapMs) observedRuntimeMs += elapsed;
  }

  const lastReading = readings[readings.length - 1];
  if (!lastReading || at - lastReading.at >= sampleIntervalMs) {
    readings.push({ at, soilPercent, tankPercent });
  }

  const cutoff = at - retentionMs;
  return {
    readings: readings.filter((point) => point.at >= cutoff).slice(-maxReadings),
    pumpEvents: pumpEvents.filter((event) => event.at >= cutoff).slice(-maxPumpEvents),
    observedRuntimeMs,
    lastObservedAt: at,
    lastPumpOn: pumpOn,
  };
}
