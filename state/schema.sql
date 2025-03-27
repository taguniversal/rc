CREATE TABLE triples (
  psi TEXT,
  subject TEXT,
  predicate TEXT,
  object TEXT,
  UNIQUE (psi, subject, predicate, object)
);
