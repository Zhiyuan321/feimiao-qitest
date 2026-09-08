BEGIN IMMEDIATE;
ALTER TABLE sources ADD COLUMN spectrum_mode TEXT NOT NULL DEFAULT 'EI';
ALTER TABLE sources ADD COLUMN instrument_family TEXT NOT NULL DEFAULT 'unspecified';
ALTER TABLE sources ADD COLUMN validation_status TEXT NOT NULL DEFAULT 'reference-only';
ALTER TABLE sources ADD COLUMN match_eligible INTEGER NOT NULL DEFAULT 0;
UPDATE sources
SET spectrum_mode='EI',
    instrument_family='GC-EI-MS reference library',
    validation_status='NIST-QA reference; QITest hardware compatibility pending',
    match_eligible=0
WHERE name='SWGDRUG' AND version='3.14';
PRAGMA user_version=2;
COMMIT;
