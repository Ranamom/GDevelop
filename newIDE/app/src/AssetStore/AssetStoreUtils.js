// @flow
import {
  type PublicAssetPacks,
  type PrivateAssetPack,
  type PublicAssetPack,
} from '../Utils/GDevelopServices/Asset';
import { type PrivateAssetPackListingData } from '../Utils/GDevelopServices/Shop';

/**
 * A simple slug generator that allows to link to asset packs on
 * the app and on the website.
 *
 * The website has a similar implementation (which is at least retro-compatible),
 * so that asset packs can be opened from an URL containing their slug.
 */
const slugify = (incString: string): string => {
  const p = ['.', '=', '-'];
  const s = '-';

  return incString
    .toLowerCase()
    .replace(/ü/g, 'ue')
    .replace(/ä/g, 'ae')
    .replace(/ö/g, 'oe')
    .replace(/ß/g, 'ss')
    .replace(new RegExp('[' + p.join('') + ']', 'g'), ' ') //  replace preserved characters with spaces
    .replace(/-{2,}/g, ' ') //  remove duplicate spaces
    .replace(/^\s\s*/, '')
    .replace(/\s\s*$/, '') //  trim both sides of string
    .replace(/[^\w ]/gi, '') //  replaces all non-alphanumeric with empty string
    .replace(/[ ]/gi, s); //  Convert spaces to dashes
};

const getPublicAssetPackUserFriendlySlug = (
  publicAssetPack: PublicAssetPack
) => {
  return `${slugify(publicAssetPack.name)}-${slugify(publicAssetPack.tag)}`;
};

const getIdFromPrivateAssetPackUserFriendlySlug = (slug: string) =>
  slug.slice(-36);

const findPublicAssetPackWithUserFriendlySlug = (
  publicAssetPacks: PublicAssetPacks,
  userFriendlySlug: string
): PublicAssetPack | null => {
  for (const publicAssetPack of publicAssetPacks.starterPacks) {
    const publicAssetPackUserFriendlySlug = getPublicAssetPackUserFriendlySlug(
      publicAssetPack
    );
    if (publicAssetPackUserFriendlySlug === userFriendlySlug)
      return publicAssetPack;
  }

  return null;
};

export const getAssetPackFromUserFriendlySlug = ({
  receivedAssetPacks,
  publicAssetPacks,
  userFriendlySlug,
}: {|
  receivedAssetPacks: Array<PrivateAssetPack>,
  publicAssetPacks: PublicAssetPacks,
  userFriendlySlug: string,
|}): PublicAssetPack | PrivateAssetPack | null => {
  const receivedAssetPackId = getIdFromPrivateAssetPackUserFriendlySlug(
    userFriendlySlug
  );
  const receivedAssetPack = receivedAssetPacks.find(
    privateAssetPack => receivedAssetPackId === privateAssetPack.id
  );
  if (receivedAssetPack) return receivedAssetPack;

  const publicAssetPack = findPublicAssetPackWithUserFriendlySlug(
    publicAssetPacks,
    userFriendlySlug
  );
  if (publicAssetPack) return publicAssetPack;

  return null;
};

export const getPrivateAssetPackListingData = ({
  privateAssetPacks,
  userFriendlySlug,
}: {|
  privateAssetPacks: Array<PrivateAssetPackListingData>,
  userFriendlySlug: string,
|}): ?PrivateAssetPackListingData => {
  const privateAssetPackId = getIdFromPrivateAssetPackUserFriendlySlug(
    userFriendlySlug
  );
  const privateAssetPack = privateAssetPacks.find(
    privateAssetPack => privateAssetPackId === privateAssetPack.id
  );
  if (privateAssetPack) return privateAssetPack;

  return null;
};
