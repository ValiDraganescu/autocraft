export const fail = (message) => {
  console.error(`build: ${message}`);
  process.exit(1);
};
