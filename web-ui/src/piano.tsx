// Hands the store and the link to every component, so tests can give them their own.

import { createContext, useContext, useMemo, type ReactNode } from 'react';
import { useStore } from 'zustand';
import type { Command, SettingValues } from '@piano-lab/web-api';
import type { PianoStore, PianoStoreApi } from './store.ts';

/** What components send to piano-lab (`Live` fits). */
export interface Sender {
    send(command: Command): boolean;
    setSettings(values: SettingValues): boolean;
}

interface Piano {
    store: PianoStoreApi;
    sender: Sender;
}

const PianoContext = createContext<Piano | undefined>(undefined);

/** Gives `children` the store and the link. */
export function PianoProvider({
    store,
    sender,
    children,
}: Piano & { children: ReactNode }): ReactNode {
    const piano = useMemo(() => ({ store, sender }), [store, sender]);
    return <PianoContext value={piano}>{children}</PianoContext>;
}

function usePianoContext(): Piano {
    const piano = useContext(PianoContext);
    if (piano === undefined) throw new Error('usePiano needs a PianoProvider');
    return piano;
}

/** Picks something out of the store, re-rendering when it changes. */
export function usePiano<T>(select: (state: PianoStore) => T): T {
    return useStore(usePianoContext().store, select);
}

/** The link, to send commands. */
export function useSender(): Sender {
    return usePianoContext().sender;
}
