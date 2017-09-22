/*
 * Copyright (c) 2017 Magewell Electronics Co., Ltd. (Nanjing),
 * All rights reserved.
 * Author: Yong Deng <yong.deng@magewell.com>
 *
 * Based on drivers/media/platform/xilinx/xilinx-vipp.c
 * Copyright (C) 2013-2015 Ideas on Board
 * Copyright (C) 2013-2015 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/of_graph.h>

#include "sun6i_csi.h"

/*
 * struct sun6i_graph_entity - Entity in the video graph
 * @list: list entry in a graph entities list
 * @node: the entity's DT node
 * @entity: media entity, from the corresponding V4L2 subdev
 * @asd: subdev asynchronous registration information
 * @subdev: V4L2 subdev
 */
struct sun6i_graph_entity {
	struct list_head		list;
	struct device_node		*node;
	struct media_entity		*entity;

	struct v4l2_async_subdev	asd;
	struct v4l2_subdev		*subdev;
};

/* -----------------------------------------------------------------------------
 * Graph Management
 */

static struct sun6i_graph_entity *
sun6i_graph_find_entity(struct sun6i_csi *csi,
			const struct device_node *node)
{
	struct sun6i_graph_entity *entity;

	list_for_each_entry(entity, &csi->entities, list) {
		if (entity->node == node)
			return entity;
	}

	return NULL;
}

static int sun6i_graph_build_one(struct sun6i_csi *csi,
				 struct sun6i_graph_entity *entity)
{
	u32 link_flags = MEDIA_LNK_FL_ENABLED;
	struct media_entity *local = entity->entity;
	struct media_entity *remote;
	struct media_pad *local_pad;
	struct media_pad *remote_pad;
	struct sun6i_graph_entity *ent;
	struct v4l2_fwnode_link link;
	struct device_node *ep = NULL;
	struct device_node *next;
	int ret = 0;

	dev_dbg(csi->dev, "creating links for entity %s\n", local->name);

	while (1) {
		/* Get the next endpoint and parse its link. */
		next = of_graph_get_next_endpoint(entity->node, ep);
		if (next == NULL)
			break;

		of_node_put(ep);
		ep = next;

		dev_dbg(csi->dev, "processing endpoint %s\n", ep->full_name);

		ret = v4l2_fwnode_parse_link(of_fwnode_handle(ep), &link);
		if (ret < 0) {
			dev_err(csi->dev, "failed to parse link for %s\n",
				ep->full_name);
			continue;
		}

		/* Skip sink ports, they will be processed from the other end of
		 * the link.
		 */
		if (link.local_port >= local->num_pads) {
			dev_err(csi->dev, "invalid port number %u on %s\n",
				link.local_port,
				to_of_node(link.local_node)->full_name);
			v4l2_fwnode_put_link(&link);
			ret = -EINVAL;
			break;
		}

		local_pad = &local->pads[link.local_port];

		if (local_pad->flags & MEDIA_PAD_FL_SINK) {
			dev_dbg(csi->dev, "skipping sink port %s:%u\n",
				to_of_node(link.local_node)->full_name,
				link.local_port);
			v4l2_fwnode_put_link(&link);
			continue;
		}

		/* Skip video node, they will be processed separately. */
		if (link.remote_node == of_fwnode_handle(csi->dev->of_node)) {
			dev_dbg(csi->dev, "skipping CSI port %s:%u\n",
				to_of_node(link.local_node)->full_name,
				link.local_port);
			v4l2_fwnode_put_link(&link);
			continue;
		}

		/* Find the remote entity. */
		ent = sun6i_graph_find_entity(csi,
					      to_of_node(link.remote_node));
		if (ent == NULL) {
			dev_err(csi->dev, "no entity found for %s\n",
				to_of_node(link.remote_node)->full_name);
			v4l2_fwnode_put_link(&link);
			ret = -ENODEV;
			break;
		}

		remote = ent->entity;

		if (link.remote_port >= remote->num_pads) {
			dev_err(csi->dev, "invalid port number %u on %s\n",
				link.remote_port,
				to_of_node(link.remote_node)->full_name);
			v4l2_fwnode_put_link(&link);
			ret = -EINVAL;
			break;
		}

		remote_pad = &remote->pads[link.remote_port];

		v4l2_fwnode_put_link(&link);

		/* Create the media link. */
		dev_dbg(csi->dev, "creating %s:%u -> %s:%u link\n",
			local->name, local_pad->index,
			remote->name, remote_pad->index);

		ret = media_create_pad_link(local, local_pad->index,
					    remote, remote_pad->index,
					    link_flags);
		if (ret < 0) {
			dev_err(csi->dev,
				"failed to create %s:%u -> %s:%u link\n",
				local->name, local_pad->index,
				remote->name, remote_pad->index);
			break;
		}
	}

	of_node_put(ep);
	return ret;
}

static int sun6i_graph_build_video(struct sun6i_csi *csi)
{
	u32 link_flags = MEDIA_LNK_FL_ENABLED;
	struct device_node *node = csi->dev->of_node;
	struct media_entity *source;
	struct media_entity *sink;
	struct media_pad *source_pad;
	struct media_pad *sink_pad;
	struct sun6i_graph_entity *ent;
	struct v4l2_fwnode_link link;
	struct device_node *ep = NULL;
	struct device_node *next;
	struct sun6i_video *video = &csi->video;
	int ret = 0;

	dev_dbg(csi->dev, "creating link for video node\n");

	while (1) {
		/* Get the next endpoint and parse its link. */
		next = of_graph_get_next_endpoint(node, ep);
		if (next == NULL)
			break;

		of_node_put(ep);
		ep = next;

		dev_dbg(csi->dev, "processing endpoint %s\n", ep->full_name);

		ret = v4l2_fwnode_parse_link(of_fwnode_handle(ep), &link);
		if (ret < 0) {
			dev_err(csi->dev, "failed to parse link for %s\n",
				ep->full_name);
			continue;
		}

		/* Save the video port settings */
		ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep),
						 &csi->v4l2_ep);
		if (ret) {
			ret = -EINVAL;
			dev_err(csi->dev, "Could not parse the endpoint\n");
			v4l2_fwnode_put_link(&link);
			break;
		}

		dev_dbg(csi->dev, "creating link for video node %s\n",
			video->vdev.name);

		/* Find the remote entity. */
		ent = sun6i_graph_find_entity(csi,
					      to_of_node(link.remote_node));
		if (ent == NULL) {
			dev_err(csi->dev, "no entity found for %s\n",
				to_of_node(link.remote_node)->full_name);
			v4l2_fwnode_put_link(&link);
			ret = -ENODEV;
			break;
		}

		if (link.remote_port >= ent->entity->num_pads) {
			dev_err(csi->dev, "invalid port number %u on %s\n",
				link.remote_port,
				to_of_node(link.remote_node)->full_name);
			v4l2_fwnode_put_link(&link);
			ret = -EINVAL;
			break;
		}

		source = ent->entity;
		source_pad = &source->pads[link.remote_port];
		sink = &video->vdev.entity;
		sink_pad = &video->pad;

		v4l2_fwnode_put_link(&link);

		/* Create the media link. */
		dev_dbg(csi->dev, "creating %s:%u -> %s:%u link\n",
			source->name, source_pad->index,
			sink->name, sink_pad->index);

		ret = media_create_pad_link(source, source_pad->index,
					    sink, sink_pad->index,
					    link_flags);
		if (ret < 0) {
			dev_err(csi->dev,
				"failed to create %s:%u -> %s:%u link\n",
				source->name, source_pad->index,
				sink->name, sink_pad->index);
			break;
		}

		/* Notify video node */
		ret = media_entity_call(sink, link_setup, sink_pad, source_pad,
					link_flags);
		if (ret == -ENOIOCTLCMD)
			ret = 0;

		/* found one */
		break;
	}

	of_node_put(ep);
	return ret;
}

static int sun6i_graph_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct sun6i_csi *csi =
			container_of(notifier, struct sun6i_csi, notifier);
	struct sun6i_graph_entity *entity;
	int ret;

	dev_dbg(csi->dev, "notify complete, all subdevs registered\n");

	/* Create links for every entity. */
	list_for_each_entry(entity, &csi->entities, list) {
		ret = sun6i_graph_build_one(csi, entity);
		if (ret < 0)
			return ret;
	}

	/* Create links for video node. */
	ret = sun6i_graph_build_video(csi);
	if (ret < 0)
		return ret;

	ret = v4l2_device_register_subdev_nodes(&csi->v4l2_dev);
	if (ret < 0)
		dev_err(csi->dev, "failed to register subdev nodes\n");

	return media_device_register(&csi->media_dev);
}

static int sun6i_graph_notify_bound(struct v4l2_async_notifier *notifier,
				    struct v4l2_subdev *subdev,
				    struct v4l2_async_subdev *asd)
{
	struct sun6i_csi *csi =
			container_of(notifier, struct sun6i_csi, notifier);
	struct sun6i_graph_entity *entity;

	/* Locate the entity corresponding to the bound subdev and store the
	 * subdev pointer.
	 */
	list_for_each_entry(entity, &csi->entities, list) {
		if (entity->node != subdev->dev->of_node)
			continue;

		if (entity->subdev) {
			dev_err(csi->dev, "duplicate subdev for node %s\n",
				entity->node->full_name);
			return -EINVAL;
		}

		dev_dbg(csi->dev, "subdev %s bound\n", subdev->name);
		entity->entity = &subdev->entity;
		entity->subdev = subdev;
		return 0;
	}

	dev_err(csi->dev, "no entity for subdev %s\n", subdev->name);
	return -EINVAL;
}

static int sun6i_graph_parse_one(struct sun6i_csi *csi,
				 struct device_node *node)
{
	struct sun6i_graph_entity *entity;
	struct device_node *remote;
	struct device_node *ep = NULL;
	int ret = 0;

	dev_dbg(csi->dev, "parsing node %s\n", node->full_name);

	while (1) {
		ep = of_graph_get_next_endpoint(node, ep);
		if (ep == NULL)
			break;

		dev_dbg(csi->dev, "handling endpoint %s\n", ep->full_name);

		remote = of_graph_get_remote_port_parent(ep);
		if (remote == NULL) {
			ret = -EINVAL;
			break;
		}

		/* Skip entities that we have already processed. */
		if (remote == csi->dev->of_node ||
		    sun6i_graph_find_entity(csi, remote)) {
			of_node_put(remote);
			continue;
		}

		entity = devm_kzalloc(csi->dev, sizeof(*entity), GFP_KERNEL);
		if (entity == NULL) {
			of_node_put(remote);
			ret = -ENOMEM;
			break;
		}

		entity->node = remote;
		entity->asd.match_type = V4L2_ASYNC_MATCH_FWNODE;
		entity->asd.match.fwnode.fwnode = of_fwnode_handle(remote);
		list_add_tail(&entity->list, &csi->entities);
		csi->num_subdevs++;
	}

	of_node_put(ep);
	return ret;
}

static int sun6i_graph_parse(struct sun6i_csi *csi)
{
	struct sun6i_graph_entity *entity;
	int ret;

	/*
	 * Walk the links to parse the full graph. Start by parsing the
	 * composite node and then parse entities in turn. The list_for_each
	 * loop will handle entities added at the end of the list while walking
	 * the links.
	 */
	ret = sun6i_graph_parse_one(csi, csi->dev->of_node);
	if (ret < 0)
		return 0;

	list_for_each_entry(entity, &csi->entities, list) {
		ret = sun6i_graph_parse_one(csi, entity->node);
		if (ret < 0)
			break;
	}

	return ret;
}

static void sun6i_graph_cleanup(struct sun6i_csi *csi)
{
	struct sun6i_graph_entity *entityp;
	struct sun6i_graph_entity *entity;

	v4l2_async_notifier_unregister(&csi->notifier);

	list_for_each_entry_safe(entity, entityp, &csi->entities, list) {
		of_node_put(entity->node);
		list_del(&entity->list);
	}
}

static int sun6i_graph_init(struct sun6i_csi *csi)
{
	struct sun6i_graph_entity *entity;
	struct v4l2_async_subdev **subdevs = NULL;
	unsigned int num_subdevs;
	unsigned int i;
	int ret;

	/* Parse the graph to extract a list of subdevice DT nodes. */
	ret = sun6i_graph_parse(csi);
	if (ret < 0) {
		dev_err(csi->dev, "graph parsing failed\n");
		goto done;
	}

	if (!csi->num_subdevs) {
		dev_err(csi->dev, "no subdev found in graph\n");
		goto done;
	}

	/* Register the subdevices notifier. */
	num_subdevs = csi->num_subdevs;
	subdevs = devm_kzalloc(csi->dev, sizeof(*subdevs) * num_subdevs,
			       GFP_KERNEL);
	if (subdevs == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	i = 0;
	list_for_each_entry(entity, &csi->entities, list)
		subdevs[i++] = &entity->asd;

	csi->notifier.subdevs = subdevs;
	csi->notifier.num_subdevs = num_subdevs;
	csi->notifier.bound = sun6i_graph_notify_bound;
	csi->notifier.complete = sun6i_graph_notify_complete;

	ret = v4l2_async_notifier_register(&csi->v4l2_dev, &csi->notifier);
	if (ret < 0) {
		dev_err(csi->dev, "notifier registration failed\n");
		goto done;
	}

	ret = 0;

done:
	if (ret < 0)
		sun6i_graph_cleanup(csi);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Media Controller and V4L2
 */

static void sun6i_csi_v4l2_cleanup(struct sun6i_csi *csi)
{
	v4l2_device_unregister(&csi->v4l2_dev);
	media_device_unregister(&csi->media_dev);
	media_device_cleanup(&csi->media_dev);
}

static int sun6i_csi_v4l2_init(struct sun6i_csi *csi)
{
	int ret;

	csi->media_dev.dev = csi->dev;
	strlcpy(csi->media_dev.model, "Allwinner Video Capture Device",
		sizeof(csi->media_dev.model));
	csi->media_dev.hw_revision = 0;

	media_device_init(&csi->media_dev);

	csi->v4l2_dev.mdev = &csi->media_dev;
	ret = v4l2_device_register(csi->dev, &csi->v4l2_dev);
	if (ret < 0) {
		dev_err(csi->dev, "V4L2 device registration failed (%d)\n",
			ret);
		media_device_cleanup(&csi->media_dev);
		return ret;
	}
	return 0;
}

int sun6i_csi_init(struct sun6i_csi *csi)
{
	int ret;

	csi->num_subdevs = 0;
	INIT_LIST_HEAD(&csi->entities);

	ret = sun6i_csi_v4l2_init(csi);
	if (ret < 0)
		return ret;

	ret = sun6i_video_init(&csi->video, csi, "sun6i-csi");
	if (ret < 0)
		goto v4l2_clean;

	ret = sun6i_graph_init(csi);
	if (ret < 0)
		goto video_clean;

	return 0;

video_clean:
	sun6i_video_cleanup(&csi->video);
v4l2_clean:
	sun6i_csi_v4l2_cleanup(csi);
	return ret;
}

int sun6i_csi_cleanup(struct sun6i_csi *csi)
{
	sun6i_video_cleanup(&csi->video);
	sun6i_graph_cleanup(csi);
	sun6i_csi_v4l2_cleanup(csi);

	return 0;
}

