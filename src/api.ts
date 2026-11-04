export type IrrigationMode = 'auto' | 'manual';

export interface IrrigationStatus {
  soilPercent: number;
  tankPercent: number;
  tankDistanceCm?: number | null;
  /** Legacy field from firmware that used an analog tank probe. */
  tankRaw?: number;
  temperatureC: number | null;
  humidityPercent: number | null;
  pumpOn: boolean;
  mode: IrrigationMode;
  moistureThreshold: number;
  soilValid: boolean;
  tankValid: boolean;
  climateValid: boolean;
  updatedAt: number;
}

export type FirebaseValue =
  | string
  | number
  | boolean
  | null
  | FirebaseValue[]
  | { [key: string]: FirebaseValue };

export interface IrrigationSnapshot {
  status: IrrigationStatus | null;
  sensorData: FirebaseValue | null;
  statusError: string | null;
  sensorDataError: string | null;
}

const viteEnv = (import.meta as ImportMeta & { env: Record<string, string | undefined> }).env;
const firebaseDatabaseUrl = (viteEnv.VITE_FIREBASE_DATABASE_URL ?? 'https://aquasense-68876-default-rtdb.firebaseio.com').replace(/\/$/, '');
const firebaseAuthToken = viteEnv.VITE_FIREBASE_AUTH_TOKEN;

async function firebaseRequest<T>(path: string, init?: RequestInit): Promise<T> {
  const url = new URL(`${firebaseDatabaseUrl}${path}`);
  if (firebaseAuthToken) url.searchParams.set('auth', firebaseAuthToken);
  const response = await fetch(url, {
    ...init,
    headers: { 'Content-Type': 'application/json', ...init?.headers },
    cache: 'no-store',
    signal: AbortSignal.timeout(5000),
  });
  if (!response.ok) throw new Error(`Firebase returned ${response.status}`);
  return response.json() as Promise<T>;
}

function commandId(): string {
  return crypto.randomUUID();
}

export const firebaseApi = {
  snapshot: async (): Promise<IrrigationSnapshot> => {
    const [statusResult, sensorDataResult] = await Promise.allSettled([
      firebaseRequest<IrrigationStatus | null>('/irrigation/status.json'),
      firebaseRequest<FirebaseValue | null>('/sensorData.json'),
    ]);
    return {
      status: statusResult.status === 'fulfilled' ? statusResult.value : null,
      sensorData: sensorDataResult.status === 'fulfilled' ? sensorDataResult.value : null,
      statusError: statusResult.status === 'rejected'
        ? statusResult.reason instanceof Error ? statusResult.reason.message : 'Could not read Firebase status'
        : null,
      sensorDataError: sensorDataResult.status === 'rejected'
        ? sensorDataResult.reason instanceof Error ? sensorDataResult.reason.message : 'Could not read Firebase sensor data'
        : null,
    };
  },
  configure: (moistureThreshold: number) =>
    firebaseRequest<FirebaseValue>('/irrigation/command.json', {
      method: 'PUT',
      body: JSON.stringify({ id: commandId(), sentAt: Date.now(), moistureThreshold }),
    }),
  control: (mode: IrrigationMode, pump?: boolean) =>
    firebaseRequest<FirebaseValue>('/irrigation/command.json', {
      method: 'PUT',
      body: JSON.stringify({ id: commandId(), sentAt: Date.now(), mode, ...(pump === undefined ? {} : { pump }) }),
    }),
};
